#pragma once

#include <QString>

#include <chrono>
#include <optional>

class AppConfig final
{
public:
    [[nodiscard]] static std::optional<AppConfig> fromEnvironment(QString *error = nullptr);
    [[nodiscard]] static std::optional<AppConfig> fromValues(
        QString botToken, QString channelId, QString adminUserId,
        QString ytDlpExecutable, QString publicationIntervalMinutes,
        QString *error = nullptr);

    [[nodiscard]] const QString &botToken() const noexcept;
    [[nodiscard]] const QString &channelId() const noexcept;
    [[nodiscard]] qint64 adminUserId() const noexcept;
    [[nodiscard]] const QString &ytDlpExecutable() const noexcept;
    [[nodiscard]] std::chrono::minutes publicationInterval() const noexcept;

private:
    AppConfig(QString botToken, QString channelId, qint64 adminUserId,
              QString ytDlpExecutable, std::chrono::minutes publicationInterval);

    QString m_botToken;
    QString m_channelId;
    qint64 m_adminUserId{};
    QString m_ytDlpExecutable;
    std::chrono::minutes m_publicationInterval;
};
