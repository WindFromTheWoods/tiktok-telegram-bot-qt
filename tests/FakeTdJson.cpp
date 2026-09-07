/******************************************************************************
 * @file    FakeTdJson.cpp
 * @brief   Implements a deterministic fake TDLib JSON library for integration tests.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QQueue>
#include <QWaitCondition>

namespace
{

QMutex QueueMutex;
QWaitCondition QueueReady;
QQueue<QByteArray> Events;

void enqueue(const QJsonObject& event)
{
    QMutexLocker lock(&QueueMutex);
    Events.enqueue(QJsonDocument(event).toJson(QJsonDocument::Compact));
    QueueReady.wakeOne();
}

void authorizationState(const QString& type)
{
    enqueue(QJsonObject{
        {QStringLiteral("@type"), QStringLiteral("updateAuthorizationState")},
        {QStringLiteral("authorization_state"), QJsonObject{{QStringLiteral("@type"), type}}},
    });
}

} // namespace

#ifdef Q_OS_WIN
#define TDJSON_EXPORT __declspec(dllexport)
#else
#define TDJSON_EXPORT __attribute__((visibility("default")))
#endif

extern "C"
{

    TDJSON_EXPORT int td_create_client_id()
    {
        authorizationState(QStringLiteral("authorizationStateWaitTdlibParameters"));
        return 1;
    }

    TDJSON_EXPORT void td_send(const int, const char* requestJson)
    {
        const QJsonObject request = QJsonDocument::fromJson(QByteArray(requestJson)).object();
        const QString type = request.value(QStringLiteral("@type")).toString();
        if (type == QStringLiteral("setTdlibParameters"))
        {
            authorizationState(QStringLiteral("authorizationStateWaitPhoneNumber"));
        }
        else if (type == QStringLiteral("setAuthenticationPhoneNumber"))
        {
            authorizationState(QStringLiteral("authorizationStateWaitCode"));
        }
        else if (type == QStringLiteral("checkAuthenticationCode"))
        {
            authorizationState(QStringLiteral("authorizationStateReady"));
        }
        else if (type == QStringLiteral("searchPublicChat"))
        {
            enqueue(
                QJsonObject{{QStringLiteral("@type"), QStringLiteral("chat")},
                            {QStringLiteral("id"), -100777.0},
                            {QStringLiteral("@extra"), request.value(QStringLiteral("@extra"))}});
        }
        else if (type == QStringLiteral("sendMessage"))
        {
            enqueue(QJsonObject{
                {QStringLiteral("@type"), QStringLiteral("message")},
                {QStringLiteral("id"), 123456.0},
                {QStringLiteral("chat_id"), request.value(QStringLiteral("chat_id"))},
                {QStringLiteral("scheduling_state"),
                 request.value(QStringLiteral("options"))
                     .toObject()
                     .value(QStringLiteral("scheduling_state"))},
                {QStringLiteral("@extra"), request.value(QStringLiteral("@extra"))},
            });
        }
        else if (type == QStringLiteral("getMessage"))
        {
            const auto id = request.value(QStringLiteral("message_id")).toInteger();
            QJsonObject response{
                {QStringLiteral("@extra"), request.value(QStringLiteral("@extra"))}};
            if (id == 999)
            {
                response.insert(QStringLiteral("@type"), QStringLiteral("error"));
                response.insert(QStringLiteral("code"), 404);
                response.insert(QStringLiteral("message"), QStringLiteral("Message not found"));
            }
            else
            {
                response.insert(QStringLiteral("@type"), QStringLiteral("message"));
                response.insert(QStringLiteral("id"), id);
                response.insert(QStringLiteral("chat_id"),
                                request.value(QStringLiteral("chat_id")));
                response.insert(QStringLiteral("date"), 1788000000);
                if (id == 123456)
                    response.insert(
                        QStringLiteral("scheduling_state"),
                        QJsonObject{{QStringLiteral("@type"),
                                     QStringLiteral("messageSchedulingStateSendAtDate")},
                                    {QStringLiteral("send_date"), 1788000000}});
                if (id == 888)
                    response.insert(QStringLiteral("sending_state"),
                                    QJsonObject{{QStringLiteral("@type"),
                                                 QStringLiteral("messageSendingStatePending")}});
            }
            enqueue(response);
        }
        else if (type == QStringLiteral("editMessageSchedulingState") ||
                 type == QStringLiteral("deleteMessages") || type == QStringLiteral("logOut"))
        {
            enqueue(
                QJsonObject{{QStringLiteral("@type"), QStringLiteral("ok")},
                            {QStringLiteral("@extra"), request.value(QStringLiteral("@extra"))}});
        }
        else if (type == QStringLiteral("close"))
        {
            authorizationState(QStringLiteral("authorizationStateClosed"));
        }
    }

    TDJSON_EXPORT const char* td_receive(const double timeoutSeconds)
    {
        thread_local QByteArray result;
        QMutexLocker lock(&QueueMutex);
        if (Events.isEmpty())
        {
            QueueReady.wait(&QueueMutex, static_cast<unsigned long>(timeoutSeconds * 1000.0));
        }
        if (Events.isEmpty())
        {
            return nullptr;
        }
        result = Events.dequeue();
        return result.constData();
    }

    TDJSON_EXPORT const char* td_execute(const char*)
    {
        return "{\"@type\":\"ok\"}";
    }

} // extern "C"

#undef TDJSON_EXPORT
