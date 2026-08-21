#include "app/BotController.h"

#include "Logging.h"
#include "tiktok/TikTokUrlValidator.h"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

#include <chrono>
#include <utility>

namespace {

constexpr qint64 TelegramVideoUploadLimit = 50LL * 1024LL * 1024LL;
constexpr auto LastPublicationSettingsKey = "scheduler/lastSuccessfulPublicationUtc";
constexpr auto PendingJobsSettingsKey = "catalog/pendingJobs";

QString displayTime(const QDateTime &utcTime)
{
    return utcTime.isValid()
        ? utcTime.toLocalTime().toString(QStringLiteral("dd.MM.yyyy HH:mm"))
        : QString{};
}

QJsonObject serializedJob(const DownloadJob &job)
{
    return {
        {QStringLiteral("requestChatId"), QString::number(job.requestChatId)},
        {QStringLiteral("requestUserId"), QString::number(job.requestUserId)},
        {QStringLiteral("sourceUrl"), job.sourceUrl.toString(QUrl::FullyEncoded)},
        {QStringLiteral("requestedAtUtc"),
         job.requestedAtUtc.toUTC().toString(Qt::ISODateWithMs)},
    };
}

const QString StartText = QStringLiteral(
    "Send me a TikTok video URL and I will download it and publish the video to the "
    "configured Telegram channel. Videos are published from the queue at the configured "
    "interval.");
const QString HelpText = QStringLiteral(
    "Send one HTTPS TikTok URL. Supported forms:\n"
    "https://www.tiktok.com/@username/video/123456789\n"
    "https://tiktok.com/@username/video/123456789\n"
    "https://vm.tiktok.com/XXXXXXXX/\n"
    "https://vt.tiktok.com/XXXXXXXX/\n\n"
    "Use /status to view the queue and next publication time.");

} // namespace

BotController::BotController(AppConfig config, QObject *parent)
    : QObject(parent)
    , m_config(std::move(config))
    , m_publicationInterval(m_config.publicationInterval())
    , m_downloader(m_config.ytDlpExecutable())
    , m_api(m_config.botToken())
    , m_bot(&m_api)
{
    connect(&m_bot, &TelegramBot::messageReceived, this, &BotController::onMessageReceived);
    connect(&m_bot, &TelegramBot::pollingStarted, this, &BotController::telegramReady);
    connect(&m_bot, &TelegramBot::pollingStarted, this, &BotController::processNext);
    connect(&m_bot, &TelegramBot::pollingIssue, this,
            &BotController::telegramConnectionIssue);
    connect(&m_downloader, &TikTokDownloader::downloadFinished, this,
            &BotController::onDownloadFinished);
    connect(&m_downloader, &TikTokDownloader::downloadFailed, this,
            &BotController::onDownloadFailed);
    connect(&m_api, &TelegramApi::videoSent, this, &BotController::onVideoSent);
    connect(&m_api, &TelegramApi::messageSent, this, &BotController::onMessageSent);
    m_publicationTimer.setSingleShot(true);
    connect(&m_publicationTimer, &QTimer::timeout, this, &BotController::processNext);
}

void BotController::start()
{
    if (m_shuttingDown) {
        return;
    }
    loadPendingJobs();
    loadPublicationSchedule();
    publishScheduledVideos();
    qCInfo(logApp) << "TikTok Telegram bot starting";
    m_bot.start();
}

void BotController::shutdown()
{
    if (m_shuttingDown) {
        return;
    }
    m_shuttingDown = true;
    qCInfo(logApp) << "TikTok Telegram bot shutting down";
    m_bot.stop();
    m_publicationTimer.stop();
    m_pendingJobs.clear();
    m_downloader.cancel();
    m_api.shutdown();
}

void BotController::onMessageReceived(const TelegramMessage &message)
{
    if (m_shuttingDown) {
        return;
    }
    if (message.userId != m_config.adminUserId()) {
        qCWarning(logApp) << "Rejected an unauthorized request from user" << message.userId;
        m_api.sendMessage(message.chatId, QStringLiteral("Unauthorized."));
        return;
    }

    const QString text = message.text.trimmed();
    const QString command = commandFromText(text);
    if (command == QStringLiteral("/start")) {
        m_api.sendMessage(message.chatId, StartText);
        return;
    }
    if (command == QStringLiteral("/help")) {
        m_api.sendMessage(message.chatId, HelpText);
        return;
    }
    if (command == QStringLiteral("/status")) {
        m_api.sendMessage(message.chatId, statusText());
        return;
    }

    if (!TikTokUrlValidator::isValid(text)) {
        m_api.sendMessage(message.chatId,
                          QStringLiteral("Please send a supported TikTok HTTPS video URL. "
                                         "Use /help for examples."));
        return;
    }

    DownloadJob job{message.chatId, message.userId, QUrl(text, QUrl::StrictMode),
                    QDateTime::currentDateTimeUtc()};
    const bool alreadyBusy = m_state != State::Idle || m_currentJob.has_value()
        || !m_pendingJobs.isEmpty() || publicationIsDelayed();
    m_pendingJobs.enqueue(std::move(job));
    savePendingJobs();
    publishScheduledVideos();
    qCInfo(logApp) << "TikTok URL accepted; queue length is" << m_pendingJobs.size();
    if (alreadyBusy) {
        m_api.sendMessage(
            message.chatId,
            QStringLiteral("Request queued. Queue position: %1. Publication interval: %2 minutes.")
                .arg(m_pendingJobs.size())
                .arg(m_publicationInterval.count()));
    }
    processNext();
}

void BotController::onDownloadFinished(const QString &filePath)
{
    if (m_shuttingDown || !m_currentJob.has_value() || m_state != State::Downloading) {
        return;
    }

    const QFileInfo fileInfo(filePath);
    if (!fileInfo.isFile() || fileInfo.size() <= 0) {
        onDownloadFailed(QStringLiteral("The downloaded file is missing or empty."));
        return;
    }
    if (fileInfo.size() > TelegramVideoUploadLimit) {
        sendToRequester(QStringLiteral(
            "The downloaded video is larger than Telegram's 50 MB Bot API upload limit."));
        m_downloader.cleanup();
        finishCurrent();
        return;
    }

    const double sizeMiB = static_cast<double>(fileInfo.size()) / (1024.0 * 1024.0);
    qCInfo(logApp).nospace() << "Uploading " << QString::number(sizeMiB, 'f', 1)
                            << " MB video";
    m_state = State::Uploading;
    publishScheduledVideos();
    sendToRequester(QStringLiteral("Uploading video..."));
    m_api.sendVideo(m_config.channelId(), filePath, {});
}

void BotController::onDownloadFailed(const QString &error)
{
    if (m_shuttingDown || !m_currentJob.has_value() || m_state != State::Downloading) {
        return;
    }
    sendToRequester(QStringLiteral("Download failed:\n%1").arg(error));
    m_downloader.cleanup();
    finishCurrent();
}

void BotController::onVideoSent(const bool success, const QString &error)
{
    if (m_shuttingDown || !m_currentJob.has_value() || m_state != State::Uploading) {
        return;
    }

    if (success) {
        qCInfo(logTelegram) << "Telegram upload successful";
        const QDateTime publishedAtUtc = QDateTime::currentDateTimeUtc();
        recordSuccessfulPublication(publishedAtUtc);
        emit videoPublished(m_currentJob->sourceUrl.toString(QUrl::FullyEncoded),
                            publishedAtUtc);
        sendToRequester(QStringLiteral("Published successfully."));
    } else {
        qCWarning(logTelegram) << "Telegram upload failed:" << error;
        sendToRequester(QStringLiteral("Upload failed:\n%1").arg(error));
    }
    m_downloader.cleanup();
    finishCurrent();
}

void BotController::onMessageSent(const qint64 chatId, const bool success,
                                  const QString &error)
{
    if (!success && !m_shuttingDown) {
        qCWarning(logTelegram) << "Could not send a status message to chat" << chatId << ':'
                               << error;
    }
}

void BotController::processNext()
{
    if (m_shuttingDown || m_currentJob.has_value() || m_pendingJobs.isEmpty()) {
        return;
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const qint64 delayMilliseconds = now.msecsTo(m_nextPublicationAtUtc);
    if (m_nextPublicationAtUtc.isValid() && delayMilliseconds > 0) {
        if (!m_publicationTimer.isActive()) {
            m_publicationTimer.start(std::chrono::milliseconds(delayMilliseconds));
            qCInfo(logApp) << "Next queued publication is scheduled for"
                           << m_nextPublicationAtUtc.toLocalTime().toString(Qt::ISODate);
        }
        return;
    }

    m_publicationTimer.stop();

    m_currentJob = m_pendingJobs.dequeue();
    m_state = State::Downloading;
    publishScheduledVideos();
    sendToRequester(QStringLiteral("Downloading TikTok..."));
    m_downloader.download(m_currentJob->sourceUrl);
}

void BotController::finishCurrent()
{
    m_currentJob.reset();
    m_state = State::Idle;
    savePendingJobs();
    publishScheduledVideos();
    processNext();
}

void BotController::sendToRequester(const QString &text)
{
    if (m_currentJob.has_value()) {
        m_api.sendMessage(m_currentJob->requestChatId, text);
    }
}

void BotController::loadPublicationSchedule()
{
    QSettings settings;
    const QDateTime lastPublication = QDateTime::fromString(
        settings.value(QString::fromLatin1(LastPublicationSettingsKey)).toString(),
        Qt::ISODateWithMs);
    if (!lastPublication.isValid()) {
        return;
    }

    const qint64 intervalSeconds =
        std::chrono::duration_cast<std::chrono::seconds>(m_publicationInterval).count();
    const QDateTime candidate = lastPublication.toUTC().addSecs(intervalSeconds);
    if (candidate > QDateTime::currentDateTimeUtc()) {
        m_nextPublicationAtUtc = candidate;
        qCInfo(logApp) << "Restored next publication time"
                       << candidate.toLocalTime().toString(Qt::ISODate);
    }
}

void BotController::loadPendingJobs()
{
    QSettings settings;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        settings.value(QString::fromLatin1(PendingJobsSettingsKey)).toByteArray(),
        &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        if (parseError.error != QJsonParseError::NoError
            && parseError.error != QJsonParseError::IllegalValue) {
            qCWarning(logApp) << "Could not read the saved video queue:"
                              << parseError.errorString();
        }
        return;
    }

    for (const QJsonValue &value : document.array()) {
        const QJsonObject object = value.toObject();
        bool chatIdValid = false;
        bool userIdValid = false;
        const qint64 chatId = object.value(QStringLiteral("requestChatId"))
                                  .toString()
                                  .toLongLong(&chatIdValid);
        const qint64 userId = object.value(QStringLiteral("requestUserId"))
                                  .toString()
                                  .toLongLong(&userIdValid);
        const QUrl sourceUrl(object.value(QStringLiteral("sourceUrl")).toString(),
                             QUrl::StrictMode);
        QDateTime requestedAtUtc = QDateTime::fromString(
            object.value(QStringLiteral("requestedAtUtc")).toString(),
            Qt::ISODateWithMs);
        if (!requestedAtUtc.isValid()) {
            requestedAtUtc = QDateTime::currentDateTimeUtc();
        }

        if (!chatIdValid || !userIdValid
            || !TikTokUrlValidator::isValid(sourceUrl.toString(QUrl::FullyEncoded))) {
            qCWarning(logApp) << "Ignored an invalid entry in the saved video queue";
            continue;
        }
        m_pendingJobs.enqueue(DownloadJob{chatId, userId, sourceUrl,
                                          requestedAtUtc.toUTC()});
    }

    if (!m_pendingJobs.isEmpty()) {
        qCInfo(logApp) << "Restored" << m_pendingJobs.size() << "queued video(s)";
    }
}

void BotController::savePendingJobs() const
{
    QJsonArray jobs;
    if (m_currentJob.has_value()) {
        jobs.append(serializedJob(*m_currentJob));
    }
    for (const DownloadJob &job : m_pendingJobs) {
        jobs.append(serializedJob(job));
    }

    QSettings settings;
    settings.setValue(QString::fromLatin1(PendingJobsSettingsKey),
                      QJsonDocument(jobs).toJson(QJsonDocument::Compact));
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        qCWarning(logApp) << "Could not persist the video queue";
    }
}

void BotController::publishScheduledVideos()
{
    emit scheduledVideosChanged(scheduledVideoEntries());
}

void BotController::recordSuccessfulPublication(const QDateTime &publishedAtUtc)
{
    const qint64 intervalSeconds =
        std::chrono::duration_cast<std::chrono::seconds>(m_publicationInterval).count();
    m_nextPublicationAtUtc = publishedAtUtc.addSecs(intervalSeconds);

    QSettings settings;
    settings.setValue(QString::fromLatin1(LastPublicationSettingsKey),
                      publishedAtUtc.toString(Qt::ISODateWithMs));
    settings.sync();
}

bool BotController::publicationIsDelayed() const
{
    return m_nextPublicationAtUtc.isValid()
        && QDateTime::currentDateTimeUtc() < m_nextPublicationAtUtc;
}

QVariantList BotController::scheduledVideoEntries() const
{
    QVariantList entries;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const qint64 intervalSeconds =
        std::chrono::duration_cast<std::chrono::seconds>(m_publicationInterval).count();
    QDateTime nextSlot = m_nextPublicationAtUtc.isValid() && m_nextPublicationAtUtc > now
        ? m_nextPublicationAtUtc
        : now;

    if (m_currentJob.has_value()) {
        const QString status = m_state == State::Uploading ? QStringLiteral("Uploading")
                                                           : QStringLiteral("Downloading");
        entries.append(QVariantMap{
            {QStringLiteral("url"),
             m_currentJob->sourceUrl.toString(QUrl::FullyEncoded)},
            {QStringLiteral("requestedAt"), displayTime(m_currentJob->requestedAtUtc)},
            {QStringLiteral("scheduledAt"), QString{}},
            {QStringLiteral("status"), status},
        });
        nextSlot = now.addSecs(intervalSeconds);
    }

    for (const DownloadJob &job : m_pendingJobs) {
        entries.append(QVariantMap{
            {QStringLiteral("url"), job.sourceUrl.toString(QUrl::FullyEncoded)},
            {QStringLiteral("requestedAt"), displayTime(job.requestedAtUtc)},
            {QStringLiteral("scheduledAt"), displayTime(nextSlot)},
            {QStringLiteral("status"), QStringLiteral("Scheduled")},
        });
        nextSlot = nextSlot.addSecs(intervalSeconds);
    }
    return entries;
}

QString BotController::statusText() const
{
    const int activeJobs = m_currentJob.has_value() ? 1 : 0;
    QString nextPublication = QStringLiteral("available now");
    if (publicationIsDelayed()) {
        nextPublication = m_nextPublicationAtUtc.toLocalTime().toString(Qt::ISODate);
    }

    return QStringLiteral(
               "Active job: %1\nQueued: %2\nPublication interval: %3 minutes\nNext publication: %4")
        .arg(activeJobs)
        .arg(m_pendingJobs.size())
        .arg(m_publicationInterval.count())
        .arg(nextPublication);
}

QString BotController::commandFromText(const QString &text)
{
    const QString firstToken = text.section(QLatin1Char(' '), 0, 0);
    return firstToken.section(QLatin1Char('@'), 0, 0).toLower();
}
