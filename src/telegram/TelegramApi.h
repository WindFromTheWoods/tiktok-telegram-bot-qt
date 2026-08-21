#pragma once

#include "telegram/TelegramTypes.h"

#include <QByteArray>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>

class QNetworkReply;

class TelegramApi final : public QObject
{
    Q_OBJECT

public:
    explicit TelegramApi(QString botToken, QObject *parent = nullptr);

    void getMe();
    void getUpdates(qint64 offset, int timeoutSeconds = 30);
    void sendMessage(qint64 chatId, const QString &text);
    void sendVideo(const QString &chatId, const QString &filePath, const QString &caption);
    void shutdown();

    [[nodiscard]] static TelegramApiResponse parseResponse(
        const QByteArray &payload, int httpStatusCode, const QString &networkError = {});

signals:
    void getMeFinished(bool success, const QString &error, int retryAfterSeconds);
    void updatesReceived(const QList<TelegramUpdate> &updates);
    void updatesFailed(const QString &error, int retryAfterSeconds);
    void messageSent(qint64 chatId, bool success, const QString &error);
    void videoSent(bool success, const QString &error);

private:
    [[nodiscard]] QNetworkRequest makeRequest(const QString &method) const;
    void trackReply(QNetworkReply *reply);
    [[nodiscard]] TelegramApiResponse finishReply(QNetworkReply *reply);
    [[nodiscard]] static QList<TelegramUpdate> parseUpdates(const QJsonValue &value,
                                                            QString *error);

    QString m_baseUrl;
    QNetworkAccessManager m_network;
    QSet<QNetworkReply *> m_activeReplies;
    bool m_shuttingDown{false};
};
