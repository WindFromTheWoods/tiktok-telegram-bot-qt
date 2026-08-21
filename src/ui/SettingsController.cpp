#include "ui/SettingsController.h"

#include "config/AppConfig.h"
#include "config/CredentialStore.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

#include <utility>

namespace {

constexpr auto ChannelIdSettingsKey = "telegram/channelId";
constexpr auto AdminUserIdSettingsKey = "telegram/adminUserId";
constexpr auto YtDlpSettingsKey = "downloader/ytDlpExecutable";
constexpr auto PublicationIntervalSettingsKey = "scheduler/publicationIntervalMinutes";
constexpr auto SentVideosSettingsKey = "catalog/sentVideos";
constexpr qsizetype MaximumSentVideoHistory = 200;

QString environmentOrDefault(const char *name, const QString &fallback = {})
{
    const QString value = qEnvironmentVariable(name).trimmed();
    return value.isEmpty() ? fallback : value;
}

} // namespace

SettingsController::SettingsController(QObject *parent)
    : QObject(parent)
{
    loadSettings();
    loadSentVideos();
}

SettingsController::~SettingsController()
{
    stopBot();
}

QString SettingsController::botToken() const
{
    return m_botToken;
}

QString SettingsController::channelId() const
{
    return m_channelId;
}

QString SettingsController::adminUserId() const
{
    return m_adminUserId;
}

QString SettingsController::ytDlpExecutable() const
{
    return m_ytDlpExecutable;
}

int SettingsController::publicationIntervalMinutes() const noexcept
{
    return m_publicationIntervalMinutes;
}

bool SettingsController::botRunning() const noexcept
{
    return m_botRunning;
}

QString SettingsController::statusMessage() const
{
    return m_statusMessage;
}

bool SettingsController::statusIsError() const noexcept
{
    return m_statusIsError;
}

bool SettingsController::secureSecretStorage() const noexcept
{
    return CredentialStore::usesSecureStorage();
}

QString SettingsController::secretStorageDescription() const
{
    return CredentialStore::storageDescription();
}

QVariantList SettingsController::scheduledVideos() const
{
    return m_scheduledVideos;
}

QVariantList SettingsController::sentVideos() const
{
    return m_sentVideos;
}

void SettingsController::setBotToken(const QString &value)
{
    if (m_botToken == value) {
        return;
    }
    m_botToken = value;
    emit settingsChanged();
}

void SettingsController::setChannelId(const QString &value)
{
    if (m_channelId == value) {
        return;
    }
    m_channelId = value;
    emit settingsChanged();
}

void SettingsController::setAdminUserId(const QString &value)
{
    if (m_adminUserId == value) {
        return;
    }
    m_adminUserId = value;
    emit settingsChanged();
}

void SettingsController::setYtDlpExecutable(const QString &value)
{
    if (m_ytDlpExecutable == value) {
        return;
    }
    m_ytDlpExecutable = value;
    emit settingsChanged();
}

void SettingsController::setPublicationIntervalMinutes(const int value)
{
    if (m_publicationIntervalMinutes == value) {
        return;
    }
    m_publicationIntervalMinutes = value;
    emit settingsChanged();
}

void SettingsController::saveAndStart()
{
    QString validationError;
    std::optional<AppConfig> config = AppConfig::fromValues(
        m_botToken, m_channelId, m_adminUserId, m_ytDlpExecutable,
        QString::number(m_publicationIntervalMinutes), &validationError);
    if (!config.has_value()) {
        setStatus(QStringLiteral("Invalid settings: %1").arg(validationError), true);
        return;
    }

    QString persistenceError;
    if (!persistSettings(*config, &persistenceError)) {
        setStatus(QStringLiteral("Could not save settings: %1").arg(persistenceError), true);
        return;
    }

    startWithConfiguration(std::move(*config));
}

void SettingsController::stopBot()
{
    if (m_botController) {
        m_botController->shutdown();
        m_botController.reset();
    }
    setBotRunning(false);
    setStatus(QStringLiteral("Bot stopped."), false);
}

void SettingsController::startIfConfigured()
{
    if (m_botToken.trimmed().isEmpty() || m_channelId.trimmed().isEmpty()
        || m_adminUserId.trimmed().isEmpty()) {
        return;
    }

    QString validationError;
    std::optional<AppConfig> config = AppConfig::fromValues(
        m_botToken, m_channelId, m_adminUserId, m_ytDlpExecutable,
        QString::number(m_publicationIntervalMinutes), &validationError);
    if (!config.has_value()) {
        setStatus(QStringLiteral("Saved settings are invalid: %1").arg(validationError), true);
        return;
    }
    startWithConfiguration(std::move(*config));
}

void SettingsController::setYtDlpFromUrl(const QUrl &url)
{
    if (url.isLocalFile()) {
        setYtDlpExecutable(url.toLocalFile());
    }
}

void SettingsController::loadSettings()
{
    QString credentialError;
    m_botToken = CredentialStore::readTelegramBotToken(&credentialError);
    if (m_botToken.isEmpty()) {
        m_botToken = environmentOrDefault("TELEGRAM_BOT_TOKEN");
    }

    QSettings settings;
    m_channelId = settings
                      .value(QString::fromLatin1(ChannelIdSettingsKey),
                             environmentOrDefault("TELEGRAM_CHANNEL_ID"))
                      .toString();
    m_adminUserId = settings
                        .value(QString::fromLatin1(AdminUserIdSettingsKey),
                               environmentOrDefault("TELEGRAM_ADMIN_USER_ID"))
                        .toString();
    m_ytDlpExecutable = settings
                            .value(QString::fromLatin1(YtDlpSettingsKey),
                                   environmentOrDefault("YTDLP_PATH",
                                                        QStringLiteral("yt-dlp")))
                            .toString();

    const QString environmentInterval =
        environmentOrDefault("TELEGRAM_POST_INTERVAL_MINUTES", QStringLiteral("120"));
    m_publicationIntervalMinutes =
        settings.value(QString::fromLatin1(PublicationIntervalSettingsKey),
                       environmentInterval)
            .toInt();

    if (!credentialError.isEmpty()) {
        setStatus(QStringLiteral("Could not read the saved bot token: %1").arg(credentialError),
                  true);
    }
}

void SettingsController::loadSentVideos()
{
    QSettings settings;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        settings.value(QString::fromLatin1(SentVideosSettingsKey)).toByteArray(),
        &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        return;
    }

    for (const QJsonValue &value : document.array()) {
        const QJsonObject object = value.toObject();
        const QString sourceUrl = object.value(QStringLiteral("sourceUrl")).toString();
        const QDateTime publishedAtUtc = QDateTime::fromString(
            object.value(QStringLiteral("publishedAtUtc")).toString(),
            Qt::ISODateWithMs);
        if (sourceUrl.isEmpty() || !publishedAtUtc.isValid()) {
            continue;
        }
        m_sentVideos.append(QVariantMap{
            {QStringLiteral("url"), sourceUrl},
            {QStringLiteral("publishedAt"),
             publishedAtUtc.toLocalTime().toString(QStringLiteral("dd.MM.yyyy HH:mm"))},
            {QStringLiteral("publishedAtUtc"),
             publishedAtUtc.toUTC().toString(Qt::ISODateWithMs)},
            {QStringLiteral("status"), QStringLiteral("Sent")},
        });
        if (m_sentVideos.size() >= MaximumSentVideoHistory) {
            break;
        }
    }
}

void SettingsController::saveSentVideos() const
{
    QJsonArray videos;
    for (const QVariant &value : m_sentVideos) {
        const QVariantMap video = value.toMap();
        videos.append(QJsonObject{
            {QStringLiteral("sourceUrl"), video.value(QStringLiteral("url")).toString()},
            {QStringLiteral("publishedAtUtc"),
             video.value(QStringLiteral("publishedAtUtc")).toString()},
        });
    }

    QSettings settings;
    settings.setValue(QString::fromLatin1(SentVideosSettingsKey),
                      QJsonDocument(videos).toJson(QJsonDocument::Compact));
    settings.sync();
}

void SettingsController::recordSentVideo(const QString &sourceUrl,
                                         const QDateTime &publishedAtUtc)
{
    m_sentVideos.prepend(QVariantMap{
        {QStringLiteral("url"), sourceUrl},
        {QStringLiteral("publishedAt"),
         publishedAtUtc.toLocalTime().toString(QStringLiteral("dd.MM.yyyy HH:mm"))},
        {QStringLiteral("publishedAtUtc"),
         publishedAtUtc.toUTC().toString(Qt::ISODateWithMs)},
        {QStringLiteral("status"), QStringLiteral("Sent")},
    });
    while (m_sentVideos.size() > MaximumSentVideoHistory) {
        m_sentVideos.removeLast();
    }
    saveSentVideos();
    emit videoCatalogChanged();
}

bool SettingsController::persistSettings(const AppConfig &config, QString *error)
{
    if (!CredentialStore::writeTelegramBotToken(config.botToken(), error)) {
        return false;
    }

    QSettings settings;
    settings.setValue(QString::fromLatin1(ChannelIdSettingsKey), config.channelId());
    settings.setValue(QString::fromLatin1(AdminUserIdSettingsKey),
                      QString::number(config.adminUserId()));
    settings.setValue(QString::fromLatin1(YtDlpSettingsKey), config.ytDlpExecutable());
    settings.setValue(QString::fromLatin1(PublicationIntervalSettingsKey),
                      config.publicationInterval().count());
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        if (error != nullptr) {
            *error = QStringLiteral("local settings storage error %1").arg(settings.status());
        }
        return false;
    }

    m_botToken = config.botToken();
    m_channelId = config.channelId();
    m_adminUserId = QString::number(config.adminUserId());
    m_ytDlpExecutable = config.ytDlpExecutable();
    m_publicationIntervalMinutes = static_cast<int>(config.publicationInterval().count());
    emit settingsChanged();
    return true;
}

void SettingsController::startWithConfiguration(AppConfig config)
{
    if (m_botController) {
        m_botController->shutdown();
        m_botController.reset();
    }

    m_botController = std::make_unique<BotController>(std::move(config));
    connect(m_botController.get(), &BotController::telegramReady, this, [this] {
        setStatus(QStringLiteral("Bot is running. Telegram polling is active."), false);
    });
    connect(m_botController.get(), &BotController::telegramConnectionIssue, this,
            [this](const QString &error, const int retryAfterSeconds) {
                setStatus(
                    QStringLiteral("Telegram connection error: %1 Retrying in %2 seconds.")
                        .arg(error)
                        .arg(retryAfterSeconds),
                    true);
            });
    connect(m_botController.get(), &BotController::scheduledVideosChanged, this,
            [this](const QVariantList &videos) {
                m_scheduledVideos = videos;
                emit videoCatalogChanged();
            });
    connect(m_botController.get(), &BotController::videoPublished, this,
            &SettingsController::recordSentVideo);
    m_botController->start();
    setBotRunning(true);
    setStatus(QStringLiteral("Settings saved. Connecting to Telegram…"), false);
}

void SettingsController::setBotRunning(const bool running)
{
    if (m_botRunning == running) {
        return;
    }
    m_botRunning = running;
    emit botRunningChanged();
}

void SettingsController::setStatus(QString message, const bool isError)
{
    if (m_statusMessage == message && m_statusIsError == isError) {
        return;
    }
    m_statusMessage = std::move(message);
    m_statusIsError = isError;
    emit statusChanged();
}
