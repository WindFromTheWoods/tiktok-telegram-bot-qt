/******************************************************************************
 * @file    TelegramApi.cpp
 * @brief   Implements asynchronous requests to the Telegram Bot API.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#include "telegram/TelegramApi.h"

#include "Logging.h"

#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrlQuery>

#include <chrono>
#include <cmath>
#include <utility>

namespace
{

constexpr auto FormContentType = "application/x-www-form-urlencoded";
constexpr auto RedactedBotApiUrl = "https://api.telegram.org/bot<redacted>/";

QString redactBotApiUrl(QString text)
{
    static const QRegularExpression botApiUrlPattern(
        QStringLiteral(R"(https?://api\.telegram\.org/bot[^/\s]+/)"),
        QRegularExpression::CaseInsensitiveOption);
    return text.replace(botApiUrlPattern, QString::fromLatin1(RedactedBotApiUrl));
}

std::optional<qint64> jsonInteger(const QJsonValue& value)
{
    if (!value.isDouble())
    {
        return std::nullopt;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number ||
        std::abs(number) > 9007199254740991.0)
    {
        return std::nullopt;
    }
    return static_cast<qint64>(number);
}

QByteArray formBody(const QList<QPair<QString, QString>>& fields)
{
    QUrlQuery query;
    for (const auto& [name, value] : fields)
    {
        query.addQueryItem(name, value);
    }
    return query.toString(QUrl::FullyEncoded).toUtf8();
}

QHttpPart textPart(const QString& name, const QString& value)
{
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QStringLiteral("form-data; name=\"%1\"").arg(name));
    part.setBody(value.toUtf8());
    return part;
}

QString safeUploadFileName(const QString& path)
{
    QString name = QFileInfo(path).fileName();
    name.replace(QLatin1Char('"'), QLatin1Char('_'));
    name.replace(QLatin1Char('\r'), QLatin1Char('_'));
    name.replace(QLatin1Char('\n'), QLatin1Char('_'));
    return name;
}

} // namespace

TelegramApi::TelegramApi(QString botToken, QObject* parent)
    : QObject(parent)
    , m_baseUrl(QStringLiteral("https://api.telegram.org/bot%1/").arg(std::move(botToken)))
{
}

void TelegramApi::getMe()
{
    if (m_shuttingDown)
    {
        return;
    }

    QNetworkReply* reply = m_network.get(makeRequest(QStringLiteral("getMe")));
    trackReply(reply);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply]
            {
                const TelegramApiResponse response = finishReply(reply);
                if (m_shuttingDown)
                {
                    return;
                }
                emit getMeFinished(response.ok, response.description, response.retryAfterSeconds);
            });
}

void TelegramApi::getUpdates(const qint64 offset, const int timeoutSeconds)
{
    if (m_shuttingDown)
    {
        return;
    }

    QUrl url = makeRequest(QStringLiteral("getUpdates")).url();
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("offset"), QString::number(offset));
    query.addQueryItem(QStringLiteral("timeout"), QString::number(timeoutSeconds));
    url.setQuery(query);

    QNetworkRequest request = makeRequest(QStringLiteral("getUpdates"));
    request.setUrl(url);
    QNetworkReply* reply = m_network.get(request);
    trackReply(reply);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply]
            {
                const TelegramApiResponse response = finishReply(reply);
                if (m_shuttingDown)
                {
                    return;
                }
                if (!response.ok)
                {
                    emit updatesFailed(response.description, response.retryAfterSeconds);
                    return;
                }

                QString parseError;
                const QList<TelegramUpdate> updates = parseUpdates(response.result, &parseError);
                if (!parseError.isEmpty())
                {
                    emit updatesFailed(parseError, 0);
                    return;
                }
                emit updatesReceived(updates);
            });
}

void TelegramApi::sendMessage(const qint64 chatId, const QString& text)
{
    if (m_shuttingDown)
    {
        return;
    }

    QNetworkRequest request = makeRequest(QStringLiteral("sendMessage"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArray(FormContentType));
    const QByteArray body = formBody({
        {QStringLiteral("chat_id"), QString::number(chatId)},
        {QStringLiteral("text"), text},
    });

    QNetworkReply* reply = m_network.post(request, body);
    trackReply(reply);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, chatId]
            {
                const TelegramApiResponse response = finishReply(reply);
                if (!m_shuttingDown)
                {
                    emit messageSent(chatId, response.ok, response.description);
                }
            });
}

void TelegramApi::sendVideo(const QString& chatId, const QString& filePath, const QString& caption)
{
    if (m_shuttingDown)
    {
        return;
    }

    auto* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    multiPart->append(textPart(QStringLiteral("chat_id"), chatId));
    multiPart->append(textPart(QStringLiteral("supports_streaming"), QStringLiteral("true")));
    if (!caption.isEmpty())
    {
        multiPart->append(textPart(QStringLiteral("caption"), caption));
    }

    auto* file = new QFile(filePath, multiPart);
    if (!file->open(QIODevice::ReadOnly))
    {
        const QString error = QStringLiteral("Could not open the downloaded video for upload: %1")
                                  .arg(file->errorString());
        delete multiPart;
        emit videoCompleted({false, false, error, QStringLiteral("file"), 0, {}, {}});
        emit videoSent(false, error);
        return;
    }

    QHttpPart videoPart;
    videoPart.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("video/mp4"));
    videoPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                        QStringLiteral("form-data; name=\"video\"; filename=\"%1\"")
                            .arg(safeUploadFileName(filePath)));
    videoPart.setBodyDevice(file);
    multiPart->append(videoPart);

    QNetworkRequest request = makeRequest(QStringLiteral("sendVideo"));
    request.setTransferTimeout(std::chrono::minutes(10));
    QNetworkReply* reply = m_network.post(request, multiPart);
    multiPart->setParent(reply);
    trackReply(reply);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, file]
            {
                const TelegramApiResponse response = finishReply(reply);
                file->close();
                if (!m_shuttingDown)
                {
                    emit videoCompleted(uploadResult(response));
                    emit videoSent(response.ok, response.description);
                }
            });
}

TelegramVideoResult TelegramApi::uploadResult(const TelegramApiResponse& response)
{
    TelegramVideoResult result;
    result.error = response.description;
    result.retryAfterSeconds = std::max(0, response.retryAfterSeconds);
    if (response.ok)
    {
        const QJsonObject message = response.result.toObject();
        const auto messageId = jsonInteger(message.value(QStringLiteral("message_id")));
        const auto chatId = jsonInteger(
            message.value(QStringLiteral("chat")).toObject().value(QStringLiteral("id")));
        result.success =
            messageId.has_value() && *messageId > 0 && chatId.has_value() && *chatId != 0;
        if (result.success)
        {
            result.chatId = QString::number(*chatId);
            result.messageId = QString::number(*messageId);
            return result;
        }
        result.error = QStringLiteral("Telegram accepted the request without a usable message "
                                      "identifier. Check the channel before retrying.");
    }
    // Only a well-formed API rejection proves the message was not accepted. In particular,
    // a timeout or a server error after transmitting bytes can hide a successful upload.
    const bool definitive = !response.ok && response.explicitRejection;
    result.deliveryUnknown = !definitive;
    result.category = result.deliveryUnknown      ? QStringLiteral("delivery_unknown")
                      : response.errorCode == 429 ? QStringLiteral("rate_limit")
                      : response.errorCode == 401 || response.errorCode == 403
                          ? QStringLiteral("permission")
                          : QStringLiteral("telegram_rejected");
    return result;
}

void TelegramApi::shutdown()
{
    if (m_shuttingDown)
    {
        return;
    }
    m_shuttingDown = true;

    const auto replies = m_activeReplies;
    for (QNetworkReply* reply : replies)
    {
        if (reply != nullptr)
        {
            reply->abort();
        }
    }
}

TelegramApiResponse TelegramApi::parseResponse(const QByteArray& payload, const int httpStatusCode,
                                               const QString& networkError)
{
    TelegramApiResponse response;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);

    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        response.description =
            networkError.isEmpty()
                ? QStringLiteral("Telegram returned malformed JSON: %1")
                      .arg(parseError.errorString())
                : QStringLiteral("Telegram network request failed: %1").arg(networkError);
        return response;
    }

    const QJsonObject object = document.object();
    response.ok = object.value(QStringLiteral("ok")).toBool(false);
    response.result = object.value(QStringLiteral("result"));
    response.errorCode = object.value(QStringLiteral("ok")).isBool() && !response.ok
                             ? object.value(QStringLiteral("error_code")).toInt(0)
                             : 0;
    response.description = object.value(QStringLiteral("description")).toString();
    response.explicitRejection =
        networkError.isEmpty() && object.value(QStringLiteral("ok")).isBool() && !response.ok &&
        response.errorCode >= 400 && response.errorCode < 500 &&
        (httpStatusCode == 0 || (httpStatusCode >= 400 && httpStatusCode < 500));
    if (response.errorCode == 0 && httpStatusCode >= 400)
        response.errorCode = httpStatusCode;
    response.retryAfterSeconds = object.value(QStringLiteral("parameters"))
                                     .toObject()
                                     .value(QStringLiteral("retry_after"))
                                     .toInt(0);

    if (httpStatusCode >= 400)
    {
        response.ok = false;
    }
    if (!networkError.isEmpty() && response.ok)
    {
        response.ok = false;
        response.description =
            QStringLiteral("Telegram network request failed: %1").arg(networkError);
    }
    if (!response.ok && response.description.isEmpty())
    {
        if (!networkError.isEmpty())
        {
            response.description =
                QStringLiteral("Telegram network request failed: %1").arg(networkError);
        }
        else if (httpStatusCode > 0)
        {
            response.description =
                QStringLiteral("Telegram request failed with HTTP status %1").arg(httpStatusCode);
        }
        else
        {
            response.description = QStringLiteral("Telegram API request failed");
        }
    }
    return response;
}

QNetworkRequest TelegramApi::makeRequest(const QString& method) const
{
    QNetworkRequest request(QUrl(m_baseUrl + method));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("TikTokTelegramBot/1.0"));
    request.setTransferTimeout(std::chrono::seconds(45));
    return request;
}

void TelegramApi::trackReply(QNetworkReply* reply)
{
    m_activeReplies.insert(reply);
}

TelegramApiResponse TelegramApi::finishReply(QNetworkReply* reply)
{
    m_activeReplies.remove(reply);
    const QByteArray payload = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString networkError = reply->error() == QNetworkReply::NoError
                                     ? QString()
                                     : redactBotApiUrl(reply->errorString());
    TelegramApiResponse response = parseResponse(payload, status, networkError);
    response.description = redactBotApiUrl(response.description);
    reply->deleteLater();
    return response;
}

QList<TelegramUpdate> TelegramApi::parseUpdates(const QJsonValue& value, QString* error)
{
    QList<TelegramUpdate> updates;
    if (!value.isArray())
    {
        *error = QStringLiteral("Telegram getUpdates response did not contain an array");
        return updates;
    }

    for (const QJsonValue& item : value.toArray())
    {
        if (!item.isObject())
        {
            *error = QStringLiteral("Telegram returned a malformed update");
            return {};
        }

        const QJsonObject updateObject = item.toObject();
        const auto updateId = jsonInteger(updateObject.value(QStringLiteral("update_id")));
        if (!updateId.has_value())
        {
            *error = QStringLiteral("Telegram update is missing a valid update_id");
            return {};
        }

        TelegramUpdate update;
        update.updateId = *updateId;
        const QJsonValue messageValue = updateObject.value(QStringLiteral("message"));
        if (messageValue.isObject())
        {
            const QJsonObject messageObject = messageValue.toObject();
            const auto chatId = jsonInteger(
                messageObject.value(QStringLiteral("chat")).toObject().value(QStringLiteral("id")));
            const auto userId = jsonInteger(
                messageObject.value(QStringLiteral("from")).toObject().value(QStringLiteral("id")));
            const QJsonValue textValue = messageObject.value(QStringLiteral("text"));
            if (chatId.has_value() && userId.has_value() && textValue.isString())
            {
                update.message = TelegramMessage{*chatId, *userId, textValue.toString()};
            }
        }
        updates.append(std::move(update));
    }
    return updates;
}
