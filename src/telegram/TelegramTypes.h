#pragma once

#include <QDateTime>
#include <QJsonValue>
#include <QString>
#include <QUrl>

#include <optional>

struct TelegramMessage
{
    qint64 chatId{};
    qint64 userId{};
    QString text;
};

struct TelegramUpdate
{
    qint64 updateId{};
    std::optional<TelegramMessage> message;
};

struct TelegramApiResponse
{
    bool ok{false};
    QJsonValue result;
    int errorCode{};
    QString description;
    int retryAfterSeconds{};
};

struct DownloadJob
{
    qint64 requestChatId{};
    qint64 requestUserId{};
    QUrl sourceUrl;
    QDateTime requestedAtUtc;
};
