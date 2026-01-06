#pragma once

#include <QByteArray>

#include "infra/iccf/IccfModels.hpp"

namespace sf::client::infra::iccf {

// Parses the raw SOAP XML response of GetMyGames into games.
//
// The function is namespace-tolerant: it matches element local-names, so it should
// work regardless of SOAP 1.1 vs SOAP 1.2 wrappers.
ParseGetMyGamesResult parseGetMyGamesSoapResponse(const QByteArray& xml);

} // namespace sf::client::infra::iccf
