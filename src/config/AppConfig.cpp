#include "config/AppConfig.h"

#include <QRegularExpression>
#include <QStringList>

#include <utility>

namespace {

constexpr int DefaultPublicationIntervalMinutes = 120;
constexpr int MaximumPublicationIntervalMinutes = 7 * 24 * 60;

QString environmentValue(const char *name)
{
    return qEnvironmentVariable(name).trimmed();
}

bool isValidChannelId(const QString &value)
{
    static const QRegularExpression usernamePattern(
        QStringLiteral(R"(^@[A-Za-z][A-Za-z0-9_]{4,31}$)"));
    if (usernamePattern.match(value).hasMatch()) {
        return true;
    }

    bool parsed = false;
    const qint64 numericId = value.toLongLong(&parsed);
    return parsed && numericId < 0;
}

} // namespace

AppConfig::AppConfig(QString botToken, QString channelId, const qint64 adminUserId,
                     QString ytDlpExecutable,
                     const std::chrono::minutes publicationInterval)
    : m_botToken(std::move(botToken))
    , m_channelId(std::move(channelId))
    , m_adminUserId(adminUserId)
    , m_ytDlpExecutable(std::move(ytDlpExecutable))
    , m_publicationInterval(publicationInterval)
{
}

std::optional<AppConfig> AppConfig::fromEnvironment(QString *error)
{
    return fromValues(environmentValue("TELEGRAM_BOT_TOKEN"),
                      environmentValue("TELEGRAM_CHANNEL_ID"),
                      environmentValue("TELEGRAM_ADMIN_USER_ID"),
                      environmentValue("YTDLP_PATH"),
                      environmentValue("TELEGRAM_POST_INTERVAL_MINUTES"), error);
}

std::optional<AppConfig> AppConfig::fromValues(QString botToken, QString channelId,
                                               QString adminUserId,
                                               QString ytDlpExecutable,
                                               QString publicationIntervalText,
                                               QString *error)
{
    botToken = botToken.trimmed();
    channelId = channelId.trimmed();
    adminUserId = adminUserId.trimmed();
    ytDlpExecutable = ytDlpExecutable.trimmed();
    publicationIntervalText = publicationIntervalText.trimmed();

    QStringList problems;
    if (botToken.isEmpty()) {
        problems.append(QStringLiteral("TELEGRAM_BOT_TOKEN is missing or empty"));
    }
    if (channelId.isEmpty()) {
        problems.append(QStringLiteral("TELEGRAM_CHANNEL_ID is missing or empty"));
    } else if (!isValidChannelId(channelId)) {
        problems.append(QStringLiteral(
            "TELEGRAM_CHANNEL_ID must be an @channel username or a negative numeric channel ID"));
    }

    bool adminIdParsed = false;
    const qint64 parsedAdminUserId = adminUserId.toLongLong(&adminIdParsed);
    if (adminUserId.isEmpty()) {
        problems.append(QStringLiteral("TELEGRAM_ADMIN_USER_ID is missing or empty"));
    } else if (!adminIdParsed || parsedAdminUserId <= 0) {
        problems.append(QStringLiteral("TELEGRAM_ADMIN_USER_ID must be a positive integer"));
    }

    int publicationIntervalMinutes = DefaultPublicationIntervalMinutes;
    if (!publicationIntervalText.isEmpty()) {
        bool intervalParsed = false;
        const int configuredInterval = publicationIntervalText.toInt(&intervalParsed);
        if (!intervalParsed || configuredInterval < 1
            || configuredInterval > MaximumPublicationIntervalMinutes) {
            problems.append(QStringLiteral(
                "TELEGRAM_POST_INTERVAL_MINUTES must be between 1 and 10080"));
        } else {
            publicationIntervalMinutes = configuredInterval;
        }
    }

    if (!problems.isEmpty()) {
        if (error != nullptr) {
            *error = problems.join(QStringLiteral("; "));
        }
        return std::nullopt;
    }

    if (ytDlpExecutable.isEmpty()) {
        ytDlpExecutable = QStringLiteral("yt-dlp");
    }

    return AppConfig(std::move(botToken), std::move(channelId), parsedAdminUserId,
                     std::move(ytDlpExecutable),
                     std::chrono::minutes(publicationIntervalMinutes));
}

const QString &AppConfig::botToken() const noexcept
{
    return m_botToken;
}

const QString &AppConfig::channelId() const noexcept
{
    return m_channelId;
}

qint64 AppConfig::adminUserId() const noexcept
{
    return m_adminUserId;
}

const QString &AppConfig::ytDlpExecutable() const noexcept
{
    return m_ytDlpExecutable;
}

std::chrono::minutes AppConfig::publicationInterval() const noexcept
{
    return m_publicationInterval;
}
