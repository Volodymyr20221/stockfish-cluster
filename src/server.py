#!/usr/bin/env python3
"""
sf_cluster_server.py (asyncio TCP)

Protocol:
- Client -> server:
  {"type":"ping"}
  {"type":"jobs_list","include_finished":true,"limit":200}
  {"type":"job_submit_or_update","job":{"id":"job-1","opponent":"...","fen":"...","limit_type":0,"limit_value":40,"multipv":3}}
  {"type":"job_cancel","job_id":"job-1"}

- Server -> client (direct response to jobs_list/job_get):
  {"type":"jobs_list","server_id":"srv1","jobs":[...]} 
  {"type":"job_state","server_id":"srv1","job":{...} | null}

- Server -> client:
  {"type":"server_status","server_id":"srv1","status":1,"running_jobs":2,"max_jobs":4,"threads":8,"logical_cores":32}
  {"type":"job_update","job_id":"job-1","status":2,"depth":23,...,"log_line":"info ..."}
"""

import argparse
import asyncio
import json
import os
import ssl
import sqlite3
import time
from collections import deque
from dataclasses import dataclass, field
from typing import Deque, Dict, Optional, Set, Tuple, Union

# --- Enums must match C++ domain enums --------------------------------------

JOB_PENDING = 0
JOB_QUEUED = 1
JOB_RUNNING = 2
JOB_FINISHED = 3
JOB_ERROR = 4
JOB_CANCELLED = 5
JOB_STOPPED = 6

SERVER_UNKNOWN = 0
SERVER_ONLINE = 1
SERVER_DEGRADED = 2
SERVER_OFFLINE = 3


JsonVal = Union[int, str]


class JobStore:
    """SQLite persistence for job records and log lines.

    This allows clients to reconnect and restore job state, and also allows
    the server to be restarted without losing *finished* job results.

    NOTE: Running jobs cannot be resumed after a server restart (Stockfish
    processes are gone). On startup we mark such jobs as JOB_ERROR.
    """

    def __init__(self, path: str) -> None:
        self.path = path
        self.db = sqlite3.connect(self.path)
        self.db.row_factory = sqlite3.Row
        self._init_schema()

    def close(self) -> None:
        try:
            self.db.close()
        except Exception:
            pass

    def _init_schema(self) -> None:
        cur = self.db.cursor()
        cur.execute("PRAGMA journal_mode=WAL")
        cur.execute("PRAGMA synchronous=NORMAL")
        cur.execute(
            """
            CREATE TABLE IF NOT EXISTS jobs (
              id TEXT PRIMARY KEY,
              opponent TEXT,
              fen TEXT,
              limit_type INTEGER,
              limit_value INTEGER,
              multipv INTEGER,
              status INTEGER,
              created_at_ms INTEGER,
              started_at_ms INTEGER,
              finished_at_ms INTEGER,
              last_update_ms INTEGER,
              bestmove TEXT,
              last_by_mpv_json TEXT
            )
            """
        )
        cur.execute(
            """
            CREATE TABLE IF NOT EXISTS job_logs (
              job_id TEXT,
              ts_ms INTEGER,
              line TEXT
            )
            """
        )
        cur.execute("CREATE INDEX IF NOT EXISTS idx_job_logs_job_ts ON job_logs(job_id, ts_ms)")
        self.db.commit()

    def upsert_job(self, rec: "JobRecord") -> None:
        cur = self.db.cursor()
        cur.execute(
            """
            INSERT INTO jobs(
              id, opponent, fen, limit_type, limit_value, multipv, status,
              created_at_ms, started_at_ms, finished_at_ms, last_update_ms,
              bestmove, last_by_mpv_json
            ) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)
            ON CONFLICT(id) DO UPDATE SET
              opponent=excluded.opponent,
              fen=excluded.fen,
              limit_type=excluded.limit_type,
              limit_value=excluded.limit_value,
              multipv=excluded.multipv,
              status=excluded.status,
              created_at_ms=excluded.created_at_ms,
              started_at_ms=excluded.started_at_ms,
              finished_at_ms=excluded.finished_at_ms,
              last_update_ms=excluded.last_update_ms,
              bestmove=excluded.bestmove,
              last_by_mpv_json=excluded.last_by_mpv_json
            """,
            (
                rec.job_id,
                rec.opponent,
                rec.fen,
                int(rec.limit_type),
                int(rec.limit_value),
                int(rec.multipv),
                int(rec.status),
                int(rec.created_at_ms),
                int(rec.started_at_ms) if rec.started_at_ms is not None else None,
                int(rec.finished_at_ms) if rec.finished_at_ms is not None else None,
                int(rec.last_update_ms),
                rec.bestmove,
                json.dumps(rec.last_by_mpv, separators=(",", ":")),
            ),
        )
        self.db.commit()

    def append_log(self, job_id: str, ts_ms: int, line: str) -> None:
        if not line:
            return
        cur = self.db.cursor()
        cur.execute(
            "INSERT INTO job_logs(job_id, ts_ms, line) VALUES(?,?,?)",
            (job_id, int(ts_ms), str(line)),
        )
        self.db.commit()

    def fetch_log_tail(self, job_id: str, limit: int = 200) -> list[str]:
        cur = self.db.cursor()
        cur.execute(
            "SELECT line FROM job_logs WHERE job_id=? ORDER BY ts_ms DESC LIMIT ?",
            (job_id, int(limit)),
        )
        rows = [r[0] for r in cur.fetchall()]
        rows.reverse()
        return rows

    def list_jobs(self, include_finished: bool, limit: int) -> list[sqlite3.Row]:
        terminal = (JOB_FINISHED, JOB_ERROR, JOB_CANCELLED, JOB_STOPPED)
        cur = self.db.cursor()
        if include_finished:
            cur.execute(
                "SELECT * FROM jobs ORDER BY created_at_ms DESC LIMIT ?",
                (int(limit),),
            )
        else:
            cur.execute(
                "SELECT * FROM jobs WHERE status NOT IN (?,?,?,?) ORDER BY created_at_ms DESC LIMIT ?",
                (*terminal, int(limit)),
            )
        return list(cur.fetchall())

    def load_recent_into_memory(self, limit: int = 500) -> list[sqlite3.Row]:
        cur = self.db.cursor()
        cur.execute("SELECT * FROM jobs ORDER BY created_at_ms DESC LIMIT ?", (int(limit),))
        return list(cur.fetchall())

    def mark_incomplete_as_error(self, now_ms: int) -> list[str]:
        """Mark queued/running jobs as error after server restart.

        Returns list of job IDs that were affected.
        """
        cur = self.db.cursor()
        cur.execute(
            "SELECT id FROM jobs WHERE status IN (?,?,?)",
            (JOB_PENDING, JOB_QUEUED, JOB_RUNNING),
        )
        ids = [str(r[0]) for r in cur.fetchall()]
        if not ids:
            return []
        cur.execute(
            """
            UPDATE jobs
               SET status=?, finished_at_ms=COALESCE(finished_at_ms, ?), last_update_ms=?
             WHERE status IN (?,?,?)
            """,
            (JOB_ERROR, int(now_ms), int(now_ms), JOB_PENDING, JOB_QUEUED, JOB_RUNNING),
        )
        self.db.commit()
        return ids



def epoch_ms() -> int:
    """Wall-clock milliseconds since Unix epoch."""
    return int(time.time() * 1000)


def parse_info_line(line: str) -> Dict[str, JsonVal]:
    """
    Parse a UCI 'info' line into fields:
      depth, seldepth, score_cp/score_mate, nodes, nps, pv
    """
    tokens = line.split()
    out: Dict[str, JsonVal] = {}
    i = 0
    while i < len(tokens):
        tok = tokens[i]
        if tok == "depth" and i + 1 < len(tokens):
            out["depth"] = int(tokens[i + 1])
            i += 1
        elif tok == "seldepth" and i + 1 < len(tokens):
            out["seldepth"] = int(tokens[i + 1])
            i += 1
        elif tok == "score" and i + 2 < len(tokens):
            stype = tokens[i + 1]
            sval = tokens[i + 2]
            if stype == "cp":
                out["score_cp"] = int(sval)
            elif stype == "mate":
                out["score_mate"] = int(sval)
            i += 2
        elif tok == "nodes" and i + 1 < len(tokens):
            out["nodes"] = int(tokens[i + 1])
            i += 1
        elif tok == "nps" and i + 1 < len(tokens):
            out["nps"] = int(tokens[i + 1])
            i += 1
        elif tok == "multipv" and i + 1 < len(tokens):
            out["multipv"] = int(tokens[i + 1])
            i += 1
        elif tok == "pv" and i + 1 < len(tokens):
            out["pv"] = " ".join(tokens[i + 1 :])
            break
        i += 1
    return out


def parse_bestmove_line(line: str) -> Dict[str, str]:
    parts = line.split()
    if len(parts) >= 2:
        return {"bestmove": parts[1]}
    return {}


@dataclass
class PendingJob:
    job_id: str
    opponent: str
    fen: str
    limit_type: int
    limit_value: int
    multipv: int = 1


@dataclass
class JobRecord:
    """Persistent-ish job state (lives as long as the server process lives).

    Used to let clients reconnect and query results even if they missed
    live `job_update` events.
    """

    job_id: str
    opponent: str = ""
    fen: str = ""
    limit_type: int = 0
    limit_value: int = 0
    multipv: int = 1

    status: int = JOB_PENDING

    created_at_ms: int = field(default_factory=epoch_ms)
    started_at_ms: Optional[int] = None
    finished_at_ms: Optional[int] = None
    last_update_ms: int = field(default_factory=epoch_ms)

    # Latest per-multipv analysis line.
    last_by_mpv: Dict[int, Dict[str, JsonVal]] = field(default_factory=dict)

    # Last seen bestmove.
    bestmove: str = ""

    # Keep a bounded log tail.
    log: Deque[str] = field(default_factory=lambda: deque(maxlen=2000))

    def append_log(self, line: str) -> None:
        if line:
            self.log.append(line)


@dataclass
class EngineJobRunner:
    server: "ClusterServer"
    job: PendingJob
    cancel_event: asyncio.Event = field(default_factory=asyncio.Event)
    proc: Optional[asyncio.subprocess.Process] = None

    def request_cancel(self) -> None:
        self.cancel_event.set()

    async def run(self) -> Tuple[int, Dict[str, JsonVal]]:
        """
        Run stockfish and stream updates.
        Returns (final_status, last_fields)
        """
        last_by_mpv: Dict[int, Dict[str, JsonVal]] = {}
        job_id = self.job.job_id

        try:
            self.proc = await asyncio.create_subprocess_exec(
                self.server.stockfish_path,
                stdin=asyncio.subprocess.PIPE,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.STDOUT,
            )
            assert self.proc.stdin and self.proc.stdout

            async def send(cmd: str) -> None:
                self.proc.stdin.write((cmd + "\n").encode("utf-8"))
                await self.proc.stdin.drain()

            # UCI init
            await send("uci")
            while True:
                line = await self.proc.stdout.readline()
                if not line:
                    raise RuntimeError("Engine closed stdout during UCI init")
                if line.decode("utf-8", errors="ignore").strip() == "uciok":
                    break

            if self.server.threads > 0:
                await send(f"setoption name Threads value {self.server.threads}")

            # MultiPV: number of principal variations requested
            mpv = int(self.job.multipv or 1)
            if mpv < 1:
                mpv = 1
            await send(f"setoption name MultiPV value {mpv}")

            await send("isready")
            while True:
                line = await self.proc.stdout.readline()
                if not line:
                    raise RuntimeError("Engine closed stdout during isready")
                if line.decode("utf-8", errors="ignore").strip() == "readyok":
                    break

            await send("ucinewgame")
            await send(f"position fen {self.job.fen}")

            if self.job.limit_type == 0:
                go_cmd = f"go depth {self.job.limit_value}"
            elif self.job.limit_type == 1:
                go_cmd = f"go movetime {self.job.limit_value}"
            elif self.job.limit_type == 2:
                go_cmd = f"go nodes {self.job.limit_value}"
            else:
                go_cmd = "go depth 20"
            await send(go_cmd)

            # Stream output
            while True:
                if self.cancel_event.is_set():
                    await send("stop")
                    # We will likely still read a final bestmove; treat as cancelled.
                line = await self.proc.stdout.readline()
                if not line:
                    raise RuntimeError("Engine terminated unexpectedly")

                s = line.decode("utf-8", errors="ignore").strip()
                if not s:
                    continue

                if s.startswith("info"):
                    parsed = parse_info_line(s)
                    mpv = int(parsed.get("multipv", 1))
                    cur = last_by_mpv.get(mpv, {})
                    cur.update(parsed)
                    cur["multipv"] = mpv
                    last_by_mpv[mpv] = cur
                    await self.server.send_job_update(
                        job_id, JOB_RUNNING, cur, log_line=s
                    )
                elif s.startswith("bestmove"):
                    bm = parse_bestmove_line(s)
                    final_status = JOB_CANCELLED if self.cancel_event.is_set() else JOB_FINISHED

                    # Use multipv=1 line if available, attach bestmove
                    fields = dict(last_by_mpv.get(1, {}))
                    fields.update(bm)
                    fields["multipv"] = 1

                    await self.server.send_job_update(
                        job_id, final_status, fields, log_line=s
                    )
                    return final_status, fields

        except Exception as exc:
            await self.server.send_job_update(
                job_id, JOB_ERROR, {}, log_line=f"[job {job_id}] Error: {exc}"
            )
            return JOB_ERROR, {}
        finally:
            if self.proc is not None:
                try:
                    if self.proc.returncode is None:
                        self.proc.kill()
                except ProcessLookupError:
                    pass
                try:
                    await self.proc.wait()
                except Exception:
                    pass


class ClusterServer:
    def __init__(
        self,
        host: str,
        port: int,
        server_id: str,
        stockfish_path: str,
        threads: int,
        max_jobs: int,
        db_path: Optional[str] = None,
        db_load_limit: int = 500,
        ssl_ctx: Optional[ssl.SSLContext] = None,
    ) -> None:
        self.host = host
        self.port = port
        self.server_id = server_id
        self.stockfish_path = stockfish_path
        self.threads = threads
        self.max_jobs = max_jobs

        self.store: Optional[JobStore] = None
        if db_path:
            self.store = JobStore(db_path)
            # After restart, unfinished jobs are lost (engine procs are gone).
            # Mark them as error so clients do not see "Running" forever.
            now = epoch_ms()
            try:
                affected = self.store.mark_incomplete_as_error(now)
                for job_id in affected:
                    self.store.append_log(job_id, now, "[server] restart: job aborted")
            except Exception:
                pass

        # If set, asyncio will serve TLS and require client certificates (mTLS).
        self.ssl_ctx = ssl_ctx

        self.clients: Set[asyncio.StreamWriter] = set()

        self.active_jobs: Dict[str, EngineJobRunner] = {}
        self.pending: Deque[PendingJob] = deque()

        # All known jobs, including finished ones, to support reconnect.
        self.job_records: Dict[str, JobRecord] = {}
        self._lock = asyncio.Lock()

        self._server: Optional[asyncio.AbstractServer] = None

        # Load last N jobs from DB so jobs_list works even after server restart.
        if self.store is not None:
            try:
                rows = self.store.load_recent_into_memory(limit=db_load_limit)
                for row in rows:
                    rec = self._row_to_record(row)
                    # Load a bounded log tail.
                    tail = self.store.fetch_log_tail(rec.job_id, limit=2000)
                    rec.log = deque(tail, maxlen=2000)
                    self.job_records[rec.job_id] = rec
            except Exception as exc:
                print(f"[server] Failed to load DB history: {exc}")

    def _row_to_record(self, row: sqlite3.Row) -> JobRecord:
        rec = JobRecord(job_id=str(row["id"]))
        rec.opponent = str(row["opponent"] or "")
        rec.fen = str(row["fen"] or "")
        rec.limit_type = int(row["limit_type"] or 0)
        rec.limit_value = int(row["limit_value"] or 0)
        rec.multipv = int(row["multipv"] or 1)
        rec.status = int(row["status"] or JOB_PENDING)
        rec.created_at_ms = int(row["created_at_ms"] or epoch_ms())
        rec.started_at_ms = int(row["started_at_ms"]) if row["started_at_ms"] is not None else None
        rec.finished_at_ms = int(row["finished_at_ms"]) if row["finished_at_ms"] is not None else None
        rec.last_update_ms = int(row["last_update_ms"] or rec.created_at_ms)
        rec.bestmove = str(row["bestmove"] or "")
        try:
            rec.last_by_mpv = json.loads(row["last_by_mpv_json"] or "{}")
        except Exception:
            rec.last_by_mpv = {}
        return rec

    # --- wire helpers --------------------------------------------------------

    async def _broadcast(self, obj: dict) -> None:
        if not self.clients:
            return
        data = json.dumps(obj, separators=(",", ":")).encode("utf-8") + b"\n"
        dead: Set[asyncio.StreamWriter] = set()
        for w in self.clients:
            try:
                w.write(data)
                await w.drain()
            except Exception:
                dead.add(w)
        for w in dead:
            try:
                w.close()
            except Exception:
                pass
            self.clients.discard(w)

    async def _send_one(self, w: asyncio.StreamWriter, obj: dict) -> None:
        data = json.dumps(obj, separators=(",", ":")).encode("utf-8") + b"\n"
        try:
            w.write(data)
            await w.drain()
        except Exception:
            try:
                w.close()
            except Exception:
                pass
            self.clients.discard(w)

    async def send_server_status(self) -> None:
        async with self._lock:
            running = len(self.active_jobs)
            max_jobs = int(self.max_jobs)
            if max_jobs > 0 and running >= max_jobs:
                status = SERVER_DEGRADED
            else:
                status = SERVER_ONLINE

            msg = {
                "type": "server_status",
                "server_id": self.server_id,
                "status": int(status),
                "running_jobs": int(running),
                "max_jobs": max_jobs,
                "threads": int(self.threads),
                "logical_cores": int(os.cpu_count() or 0),
            }

        await self._broadcast(msg)

    async def send_job_update(
        self,
        job_id: str,
        status: int,
        fields: Dict[str, JsonVal],
        log_line: Optional[str] = None,
    ) -> None:
        # Update persistent job record first (even if nobody is connected).
        ts = epoch_ms()
        rec_for_db: Optional[JobRecord] = None
        log_for_db: Optional[str] = None
        async with self._lock:
            rec = self.job_records.get(job_id)
            if rec is None:
                rec = JobRecord(job_id=job_id)
                self.job_records[job_id] = rec

            rec.status = int(status)
            rec.last_update_ms = ts

            if status == JOB_RUNNING and rec.started_at_ms is None:
                rec.started_at_ms = ts
            if status in (JOB_FINISHED, JOB_ERROR, JOB_CANCELLED, JOB_STOPPED) and rec.finished_at_ms is None:
                rec.finished_at_ms = ts

            mpv = int(fields.get("multipv", 1) or 1) if fields else 1
            if fields:
                cur = rec.last_by_mpv.get(mpv, {})
                cur.update(fields)
                cur["multipv"] = mpv
                rec.last_by_mpv[mpv] = cur

            if "bestmove" in fields:
                rec.bestmove = str(fields["bestmove"])

            if log_line is not None:
                rec.append_log(str(log_line))

            # Persist outside the lock.
            if self.store is not None:
                rec_for_db = rec
                log_for_db = str(log_line) if log_line is not None else None

        if self.store is not None and rec_for_db is not None:
            try:
                self.store.upsert_job(rec_for_db)
                if log_for_db:
                    self.store.append_log(job_id, ts, log_for_db)
            except Exception as exc:
                # Don't crash the server because DB is unavailable.
                print(f"[server] DB write failed for {job_id}: {exc}")

        msg: Dict[str, JsonVal] = {"type": "job_update", "job_id": job_id, "status": int(status)}
        for key in ("multipv", "depth", "seldepth", "score_cp", "score_mate", "nodes", "nps", "bestmove", "pv"):
            if key in fields:
                msg[key] = fields[key]
        if log_line is not None:
            msg["log_line"] = log_line
        await self._broadcast(msg)

    def _record_to_dict(self, rec: JobRecord, log_tail: int = 200) -> dict:
        # lines = all multipv lines we have, sorted by multipv.
        lines = []
        # Keys may be ints (runtime) or strings (restored from JSON).
        mpv_keys = []
        for k in rec.last_by_mpv.keys():
            try:
                mpv_keys.append(int(k))
            except Exception:
                continue
        for mpv in sorted(set(mpv_keys)):
            line = dict(rec.last_by_mpv.get(mpv, {}) or rec.last_by_mpv.get(str(mpv), {}) or {})
            line["multipv"] = int(mpv)
            lines.append(line)

        # snapshot = multipv 1 (top line), enriched with bestmove.
        snap = dict(rec.last_by_mpv.get(1, {}) or rec.last_by_mpv.get("1", {}) or {})
        if rec.bestmove:
            snap["bestmove"] = rec.bestmove
        if snap:
            snap["multipv"] = 1

        return {
            "id": rec.job_id,
            "opponent": rec.opponent,
            "fen": rec.fen,
            "limit_type": int(rec.limit_type),
            "limit_value": int(rec.limit_value),
            "multipv": int(rec.multipv),
            "status": int(rec.status),
            "created_at_ms": int(rec.created_at_ms),
            "started_at_ms": int(rec.started_at_ms) if rec.started_at_ms is not None else None,
            "finished_at_ms": int(rec.finished_at_ms) if rec.finished_at_ms is not None else None,
            "last_update_ms": int(rec.last_update_ms),
            "snapshot": snap,
            "lines": lines,
            "log_tail": list(rec.log)[-log_tail:],
        }

    # --- scheduling ----------------------------------------------------------

    async def _try_start_next(self) -> None:
        """Start queued jobs while there are free slots."""
        while True:
            async with self._lock:
                if self.max_jobs > 0 and len(self.active_jobs) >= self.max_jobs:
                    return
                if not self.pending:
                    return
                job = self.pending.popleft()
                runner = EngineJobRunner(server=self, job=job)
                self.active_jobs[job.job_id] = runner

            asyncio.create_task(self._run_job(runner))
            await self.send_server_status()

    async def _run_job(self, runner: EngineJobRunner) -> None:
        job_id = runner.job.job_id
        await self.send_job_update(job_id, JOB_RUNNING, {}, log_line="started")
        try:
            await runner.run()
        finally:
            async with self._lock:
                self.active_jobs.pop(job_id, None)
            await self.send_server_status()
            await self._try_start_next()

    # --- API -----------------------------------------------------------------

    async def submit_job(self, job: PendingJob) -> None:
        rec_for_db: Optional[JobRecord] = None
        async with self._lock:
            # already known -> ignore (idempotent). This prevents accidental
            # overwriting finished jobs if a client restarts with the same ids.
            if job.job_id in self.job_records:
                return

            # already running or queued -> ignore
            if job.job_id in self.active_jobs or any(j.job_id == job.job_id for j in self.pending):
                return

            # Create initial record (so clients can list it even while queued).
            rec = JobRecord(
                job_id=job.job_id,
                opponent=job.opponent,
                fen=job.fen,
                limit_type=int(job.limit_type),
                limit_value=int(job.limit_value),
                multipv=int(job.multipv or 1),
            )
            self.job_records[job.job_id] = rec

            if self.store is not None:
                rec_for_db = rec

            if self.max_jobs > 0 and len(self.active_jobs) >= self.max_jobs:
                self.pending.append(job)
                queued = True
            else:
                queued = False
                runner = EngineJobRunner(server=self, job=job)
                self.active_jobs[job.job_id] = runner
                asyncio.create_task(self._run_job(runner))

        if queued:
            await self.send_job_update(job.job_id, JOB_QUEUED, {}, log_line="queued")
        await self.send_server_status()

        if self.store is not None and rec_for_db is not None:
            try:
                self.store.upsert_job(rec_for_db)
                self.store.append_log(job.job_id, epoch_ms(), "submitted")
            except Exception as exc:
                print(f"[server] DB write failed for {job.job_id}: {exc}")

        if queued:
            # In case capacity is unlimited or became free.
            await self._try_start_next()

    async def cancel_job(self, job_id: str) -> None:
        async with self._lock:
            runner = self.active_jobs.get(job_id)
            if runner:
                runner.request_cancel()
                return

            # If queued - remove from queue
            for i, j in enumerate(self.pending):
                if j.job_id == job_id:
                    del self.pending[i]
                    break
            else:
                return

        await self.send_job_update(job_id, JOB_CANCELLED, {}, log_line="cancelled (queued)")
        await self.send_server_status()
        await self._try_start_next()

    # --- protocol ------------------------------------------------------------

    async def handle_message(self, obj: dict, writer: asyncio.StreamWriter) -> None:
        msg_type = obj.get("type")
        if msg_type == "ping":
            await self.send_server_status()
            return

        if msg_type == "jobs_list":
            include_finished = bool(obj.get("include_finished", True))
            limit = int(obj.get("limit", 200) or 200)
            async with self._lock:
                recs = list(self.job_records.values())
                if not include_finished:
                    recs = [r for r in recs if int(r.status) not in (JOB_FINISHED, JOB_ERROR, JOB_CANCELLED, JOB_STOPPED)]
                recs.sort(key=lambda r: int(r.created_at_ms), reverse=True)
                recs = recs[: max(0, limit)]
                msg = {
                    "type": "jobs_list",
                    "server_id": self.server_id,
                    "jobs": [self._record_to_dict(r) for r in recs],
                }
            await self._send_one(writer, msg)
            return

        if msg_type == "job_get":
            job_id = str(obj.get("job_id", ""))
            if not job_id:
                return
            log_tail = int(obj.get("log_tail", 2000) or 2000)
            log_tail = max(0, min(log_tail, 20000))
            async with self._lock:
                rec = self.job_records.get(job_id)
                # If DB is enabled, load a fresh tail for this job on demand.
                if self.store is not None and rec is not None and log_tail > 0:
                    try:
                        tail = self.store.fetch_log_tail(job_id, limit=log_tail)
                        rec.log = deque(tail, maxlen=max(2000, log_tail))
                    except Exception:
                        pass
                msg = {
                    "type": "job_state",
                    "server_id": self.server_id,
                    "job": self._record_to_dict(rec, log_tail=log_tail) if rec else None,
                }
            await self._send_one(writer, msg)
            return

        if msg_type == "job_submit_or_update":
            job_obj = obj.get("job") or {}
            job_id = str(job_obj.get("id", ""))
            opponent = str(job_obj.get("opponent", ""))
            fen = str(job_obj.get("fen", ""))
            if not job_id or not fen:
                return
            limit_type = int(job_obj.get("limit_type", 0))
            limit_value = int(job_obj.get("limit_value", 30))
            multipv = int(job_obj.get("multipv", 1) or 1)
            await self.submit_job(PendingJob(job_id, opponent, fen, limit_type, limit_value, multipv))
            return

        if msg_type == "job_cancel":
            job_id = str(obj.get("job_id", ""))
            if job_id:
                await self.cancel_job(job_id)
            return

        # Unknown message: ignore.

    async def handle_client(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        addr = writer.get_extra_info("peername")
        print(f"[server] Client connected: {addr}")
        self.clients.add(writer)
        await self.send_server_status()

        try:
            while True:
                line = await reader.readline()
                if not line:
                    break
                line = line.strip()
                if not line:
                    continue
                try:
                    obj = json.loads(line.decode("utf-8", errors="ignore"))
                except json.JSONDecodeError:
                    continue
                await self.handle_message(obj, writer)
        finally:
            print(f"[server] Client disconnected: {addr}")
            try:
                writer.close()
                await writer.wait_closed()
            except Exception:
                pass
            self.clients.discard(writer)

    async def start(self) -> None:
        self._server = await asyncio.start_server(self.handle_client, self.host, self.port, ssl=self.ssl_ctx)
        addrs = ", ".join(str(sock.getsockname()) for sock in (self._server.sockets or []))
        proto = "TLS" if self.ssl_ctx is not None else "TCP"
        print(f"[server] Listening on {addrs} ({proto}, server_id={self.server_id})")
        async with self._server:
            await self._server.serve_forever()


def parse_args(argv=None):
    p = argparse.ArgumentParser(description="Stockfish cluster server")
    p.add_argument("--server-id", required=True, help="Server id (string)")
    p.add_argument("--host", default="0.0.0.0", help="Host to bind")
    p.add_argument("--port", type=int, default=9000, help="Port to listen on")
    p.add_argument("--stockfish", required=True, help="Path to Stockfish binary")
    p.add_argument("--threads", type=int, default=32, help="Threads per job")
    p.add_argument("--max-jobs", type=int, default=1, help="Max concurrent jobs")

    # Persistence
    p.add_argument(
        "--db",
        default=None,
        help="Optional SQLite DB file to persist jobs/logs (enables restore after client/server restart)",
    )
    p.add_argument(
        "--db-load-limit",
        type=int,
        default=500,
        help="How many recent jobs to load into memory at startup (only used when --db is set)",
    )

    # TLS / mTLS
    p.add_argument("--tls-cert", help="Path to server certificate (PEM)")
    p.add_argument("--tls-key", help="Path to server private key (PEM)")
    p.add_argument("--client-ca", help="Path to CA certificate used to verify client certificates (PEM)")
    p.add_argument("--tls-min-version", default="1.2", choices=["1.2", "1.3"], help="Minimum TLS version")
    return p.parse_args(argv)


def main(argv=None) -> int:
    args = parse_args(argv)

    ssl_ctx = None
    if args.tls_cert or args.tls_key or args.client_ca:
        if not (args.tls_cert and args.tls_key and args.client_ca):
            raise SystemExit("TLS enabled but --tls-cert, --tls-key and --client-ca must all be provided")

        ssl_ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ssl_ctx.load_cert_chain(certfile=args.tls_cert, keyfile=args.tls_key)
        ssl_ctx.load_verify_locations(cafile=args.client_ca)
        ssl_ctx.verify_mode = ssl.CERT_REQUIRED

        if args.tls_min_version == "1.3":
            ssl_ctx.minimum_version = ssl.TLSVersion.TLSv1_3
        else:
            ssl_ctx.minimum_version = ssl.TLSVersion.TLSv1_2

    server = ClusterServer(
        host=args.host,
        port=args.port,
        server_id=args.server_id,
        stockfish_path=args.stockfish,
        threads=args.threads,
        max_jobs=args.max_jobs,
        db_path=args.db,
        db_load_limit=args.db_load_limit,
        ssl_ctx=ssl_ctx,
    )
    try:
        asyncio.run(server.start())
    except KeyboardInterrupt:
        print("\n[server] interrupted")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
