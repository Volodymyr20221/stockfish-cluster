#pragma once

#include <QJsonDocument>
#include <QString>
#include <vector>

#include "domain/domain_model.hpp"

namespace sf::client::ui {

class JobExporter final {
public:
    static QJsonDocument toJson(const std::vector<sf::client::domain::Job>& jobs);
    static QString       toPgn(const std::vector<sf::client::domain::Job>& jobs);

private:
    JobExporter() = delete;
};

} // namespace sf::client::ui
