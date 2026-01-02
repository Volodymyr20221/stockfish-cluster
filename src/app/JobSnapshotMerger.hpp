#pragma once

#include "domain/domain_model.hpp"

#include <algorithm>
#include <optional>
#include <utility>

namespace sf::client::app {

// A tiny merger that keeps JobManager::applyRemoteUpdate() small.
//
// Merge rules (intentionally conservative):
//  - optional fields are merged only if incoming has a value
//  - depth/selDepth/nodes/nps are monotonic (never decrease)
//  - score is merged only if incoming.score.type != None
//  - bestMove/pv are merged only if incoming string is non-empty
//  - Pv lines are upserted by multipv and kept sorted
struct JobSnapshotMerger final {
    static void merge(domain::JobSnapshot& dst, const domain::JobSnapshot& in) {
        mergeOptionalMax(dst.depth, in.depth);
        mergeOptionalMax(dst.selDepth, in.selDepth);
        mergeOptionalMax(dst.nodes, in.nodes);
        mergeOptionalMax(dst.nps, in.nps);

        if (in.score.type != domain::ScoreType::None) {
            dst.score = in.score;
        }

        if (!in.bestMove.empty()) {
            dst.bestMove = in.bestMove;
        }
        if (!in.pv.empty()) {
            dst.pv = in.pv;
        }

        if (!in.lines.empty()) {
            for (const auto& lineIn : in.lines) {
                const int mpv = (lineIn.multipv <= 0) ? 1 : lineIn.multipv;
                bool replaced = false;
                for (auto& lineDst : dst.lines) {
                    if (lineDst.multipv == mpv) {
                        lineDst = lineIn;
                        lineDst.multipv = mpv;
                        replaced = true;
                        break;
                    }
                }
                if (!replaced) {
                    auto copy = lineIn;
                    copy.multipv = mpv;
                    dst.lines.push_back(std::move(copy));
                }
            }

            std::sort(dst.lines.begin(), dst.lines.end(), [](const domain::PvLine& a, const domain::PvLine& b) {
                return a.multipv < b.multipv;
            });
        }
    }

private:
    template <typename T>
    static void mergeOptionalMax(std::optional<T>& dst, const std::optional<T>& in) {
        if (!in.has_value()) {
            return;
        }
        if (dst.has_value()) {
            dst = std::max(*dst, *in);
        } else {
            dst = in;
        }
    }
};

} // namespace sf::client::app
