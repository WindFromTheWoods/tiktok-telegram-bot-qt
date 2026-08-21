#include "telegram/TelegramBot.h"

#include "Logging.h"
#include "telegram/TelegramApi.h"

#include <QtGlobal>

#include <algorithm>
#include <chrono>

namespace {

constexpr int LongPollTimeoutSeconds = 30;
constexpr int MaximumBackoffSeconds = 30;

} // namespace

TelegramBot::TelegramBot(TelegramApi *api, QObject *parent)
    : QObject(parent)
    , m_api(api)
{
    Q_ASSERT(m_api != nullptr);
    m_pollTimer.setSingleShot(true);

    connect(&m_pollTimer, &QTimer::timeout, this, &TelegramBot::poll);
    connect(m_api, &TelegramApi::getMeFinished, this, &TelegramBot::onGetMeFinished);
    connect(m_api, &TelegramApi::updatesReceived, this, &TelegramBot::onUpdatesReceived);
    connect(m_api, &TelegramApi::updatesFailed, this, &TelegramBot::onUpdatesFailed);
}

void TelegramBot::start()
{
    if (m_running) {
        return;
    }
    m_running = true;
    m_waitingForIdentity = true;
    qCInfo(logTelegram) << "Validating Telegram bot credentials";
    m_api->getMe();
}

void TelegramBot::stop()
{
    m_running = false;
    m_pollTimer.stop();
}

void TelegramBot::poll()
{
    if (!m_running) {
        return;
    }
    if (m_waitingForIdentity) {
        m_api->getMe();
        return;
    }
    m_api->getUpdates(m_offset, LongPollTimeoutSeconds);
}

void TelegramBot::onGetMeFinished(const bool success, const QString &error,
                                  const int retryAfterSeconds)
{
    if (!m_running || !m_waitingForIdentity) {
        return;
    }

    if (success) {
        m_waitingForIdentity = false;
        m_consecutiveFailures = 0;
        qCInfo(logTelegram) << "Telegram polling started";
        emit pollingStarted();
        schedulePoll(0);
        return;
    }

    const int delay = retryAfterSeconds > 0 ? retryAfterSeconds : nextBackoffSeconds();
    qCWarning(logNetwork) << "Telegram getMe failed:" << error << "retrying in" << delay
                          << "seconds";
    emit pollingIssue(error, delay);
    m_pollTimer.start(std::chrono::seconds(delay));
}

void TelegramBot::onUpdatesReceived(const QList<TelegramUpdate> &updates)
{
    if (!m_running || m_waitingForIdentity) {
        return;
    }

    m_consecutiveFailures = 0;
    for (const TelegramUpdate &update : updates) {
        m_offset = std::max(m_offset, update.updateId + 1);
        qCDebug(logTelegram) << "Received update" << update.updateId;
        if (update.message.has_value()) {
            emit messageReceived(*update.message);
        }
    }
    schedulePoll(0);
}

void TelegramBot::onUpdatesFailed(const QString &error, const int retryAfterSeconds)
{
    if (!m_running || m_waitingForIdentity) {
        return;
    }

    const int delay = retryAfterSeconds > 0 ? retryAfterSeconds : nextBackoffSeconds();
    qCWarning(logNetwork) << "Telegram polling failed:" << error << "retrying in" << delay
                          << "seconds";
    emit pollingIssue(error, delay);
    schedulePoll(delay);
}

void TelegramBot::schedulePoll(const int delaySeconds)
{
    if (m_running) {
        m_pollTimer.start(std::chrono::seconds(std::max(0, delaySeconds)));
    }
}

int TelegramBot::nextBackoffSeconds()
{
    m_consecutiveFailures = std::min(m_consecutiveFailures + 1, 6);
    return std::min(1 << (m_consecutiveFailures - 1), MaximumBackoffSeconds);
}
