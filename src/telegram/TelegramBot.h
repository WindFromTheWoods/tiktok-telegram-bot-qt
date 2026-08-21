#pragma once

#include "telegram/TelegramTypes.h"

#include <QObject>
#include <QTimer>

class TelegramApi;

class TelegramBot final : public QObject
{
    Q_OBJECT

public:
    explicit TelegramBot(TelegramApi *api, QObject *parent = nullptr);

    void start();
    void stop();

signals:
    void messageReceived(const TelegramMessage &message);
    void pollingStarted();
    void pollingIssue(const QString &error, int retryAfterSeconds);

private slots:
    void poll();
    void onGetMeFinished(bool success, const QString &error, int retryAfterSeconds);
    void onUpdatesReceived(const QList<TelegramUpdate> &updates);
    void onUpdatesFailed(const QString &error, int retryAfterSeconds);

private:
    void schedulePoll(int delaySeconds);
    [[nodiscard]] int nextBackoffSeconds();

    TelegramApi *m_api{};
    QTimer m_pollTimer;
    qint64 m_offset{};
    int m_consecutiveFailures{};
    bool m_running{false};
    bool m_waitingForIdentity{false};
};
