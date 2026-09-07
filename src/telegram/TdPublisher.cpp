/******************************************************************************
 * @file    TdPublisher.cpp
 * @brief   Implements TDLib authorization and Telegram server scheduling.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#include "telegram/TdPublisher.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QSysInfo>

#include <algorithm>
#include <utility>

namespace
{

constexpr qint64 MaximumScheduleSeconds = 367LL * 24LL * 60LL * 60LL;

QJsonObject extra(const QString& kind, const qint64 videoId = 0, const QString& operation = {})
{
    QJsonObject object{{QStringLiteral("kind"), kind}};
    if (videoId > 0)
    {
        object.insert(QStringLiteral("video_id"), QString::number(videoId));
    }
    if (!operation.isEmpty())
    {
        object.insert(QStringLiteral("operation"), operation);
    }
    return object;
}

QJsonObject schedulingState(const QDateTime& dateTimeUtc)
{
    return {
        {QStringLiteral("@type"), QStringLiteral("messageSchedulingStateSendAtDate")},
        {QStringLiteral("send_date"), dateTimeUtc.toUTC().toSecsSinceEpoch()},
        {QStringLiteral("repeat_period"), 0},
    };
}

} // namespace

TdPublisher::TdPublisher(QObject* parent)
    : QObject(parent)
{
    connect(&m_client, &TdJsonClient::eventReceived, this, &TdPublisher::handleEvent);
    connect(&m_client, &TdJsonClient::receiveError, this,
            [this](const QString& error) { emit activity(QStringLiteral("warning"), error); });
    m_submissionTimer.setSingleShot(true);
    m_submissionTimer.setInterval(10 * 60 * 1000);
    connect(&m_submissionTimer, &QTimer::timeout, this,
            [this]
            {
                failSubmission(QStringLiteral("TDLib submission timed out. Check Telegram before "
                                              "retrying."),
                               m_pendingSubmission && m_pendingSubmission->sendStarted);
            });
    m_reconciliationTimer.setInterval(5000);
    connect(&m_reconciliationTimer, &QTimer::timeout, this,
            [this]
            {
                const auto pending = m_pendingReconciliations;
                for (auto it = pending.cbegin(); it != pending.cend(); ++it)
                {
                    if (it.value().secsTo(QDateTime::currentDateTimeUtc()) >= 30)
                    {
                        m_pendingReconciliations.remove(it.key());
                        emit videoReconciled(it.key(), QStringLiteral("unknown"), {}, {}, {},
                                             QStringLiteral("Telegram confirmation timed out."));
                    }
                }
            });
}

TdPublisher::~TdPublisher()
{
    stop();
}

bool TdPublisher::start(TdPublisherConfig config, QString* error)
{
    if (config.apiId <= 0 || config.apiHash.trimmed().isEmpty() ||
        config.databaseEncryptionKey.isEmpty())
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("TDLib API ID, API hash and database key are required.");
        }
        return false;
    }
    if (config.databaseDirectory.trimmed().isEmpty())
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("TDLib database directory is missing.");
        }
        return false;
    }
    if (!QDir().mkpath(config.databaseDirectory))
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Could not create the TDLib data directory.");
        }
        return false;
    }

    stop();
    m_config = std::move(config);
    QString startError;
    if (!m_client.start(m_config.libraryPath, &startError))
    {
        setAuthorizationState(QStringLiteral("error"), startError, false);
        if (error != nullptr)
        {
            *error = startError;
        }
        return false;
    }
    setAuthorizationState(QStringLiteral("starting"),
                          QStringLiteral("TDLib loaded. Waiting for authorization state…"), false);
    emit activity(QStringLiteral("info"), QStringLiteral("TDLib user publisher started."));
    return true;
}

void TdPublisher::stop()
{
    if (m_pendingSubmission.has_value())
    {
        failSubmission(QStringLiteral("TDLib publisher stopped."),
                       m_pendingSubmission->sendStarted);
    }
    m_client.stop();
    m_chatIds.clear();
    m_pendingRemoteVideoIds.clear();
    m_pendingReconciliations.clear();
    m_trackedMessages.clear();
    m_submissionTimer.stop();
    m_reconciliationTimer.stop();
    setAuthorizationState(QStringLiteral("stopped"), QStringLiteral("TDLib is stopped."), false);
}

void TdPublisher::logOut()
{
    QJsonObject request{{QStringLiteral("@type"), QStringLiteral("logOut")},
                        {QStringLiteral("@extra"), extra(QStringLiteral("auth"))}};
    QString error;
    if (!m_client.send(request, &error))
    {
        emit activity(QStringLiteral("error"), error);
    }
}

bool TdPublisher::isReady() const noexcept
{
    return m_ready;
}
bool TdPublisher::isBusy() const noexcept
{
    return m_pendingSubmission.has_value();
}
QString TdPublisher::authorizationState() const
{
    return m_authorizationState;
}
QString TdPublisher::statusText() const
{
    return m_statusText;
}

void TdPublisher::submitPhoneNumber(const QString& phoneNumber)
{
    m_config.phoneNumber = phoneNumber.trimmed();
    QJsonObject request{
        {QStringLiteral("@type"), QStringLiteral("setAuthenticationPhoneNumber")},
        {QStringLiteral("phone_number"), m_config.phoneNumber},
        {QStringLiteral("settings"), QJsonValue::Null},
    };
    sendAuthenticationRequest(std::move(request));
}

void TdPublisher::submitAuthenticationCode(const QString& code)
{
    QJsonObject request{{QStringLiteral("@type"), QStringLiteral("checkAuthenticationCode")},
                        {QStringLiteral("code"), code.trimmed()}};
    sendAuthenticationRequest(std::move(request));
}

void TdPublisher::submitPassword(const QString& password)
{
    QJsonObject request{{QStringLiteral("@type"), QStringLiteral("checkAuthenticationPassword")},
                        {QStringLiteral("password"), password}};
    sendAuthenticationRequest(std::move(request));
}

void TdPublisher::submitEmailAddress(const QString& emailAddress)
{
    QJsonObject request{{QStringLiteral("@type"), QStringLiteral("setAuthenticationEmailAddress")},
                        {QStringLiteral("email_address"), emailAddress.trimmed()}};
    sendAuthenticationRequest(std::move(request));
}

void TdPublisher::submitEmailCode(const QString& code)
{
    QJsonObject authentication{
        {QStringLiteral("@type"), QStringLiteral("emailAddressAuthenticationCode")},
        {QStringLiteral("code"), code.trimmed()},
    };
    QJsonObject request{{QStringLiteral("@type"), QStringLiteral("checkAuthenticationEmailCode")},
                        {QStringLiteral("code"), authentication}};
    sendAuthenticationRequest(std::move(request));
}

void TdPublisher::submitRegistration(const QString& firstName, const QString& lastName)
{
    QJsonObject request{{QStringLiteral("@type"), QStringLiteral("registerUser")},
                        {QStringLiteral("first_name"), firstName.trimmed()},
                        {QStringLiteral("last_name"), lastName.trimmed()},
                        {QStringLiteral("disable_notification"), false}};
    sendAuthenticationRequest(std::move(request));
}

bool TdPublisher::submitVideo(const qint64 videoId, const QString& channel,
                              const QString& localFilePath, const QDateTime& scheduledAtUtc,
                              const int durationSeconds, const bool publishImmediately,
                              QString* error, const QString& caption)
{
    if (!m_ready)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("The TDLib user account is not authorized.");
        }
        return false;
    }
    if (m_pendingSubmission.has_value())
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Another video is already being submitted to TDLib.");
        }
        return false;
    }
    if (!QFileInfo(localFilePath).isFile())
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("The video file does not exist.");
        }
        return false;
    }
    if (!publishImmediately)
    {
        const qint64 seconds = QDateTime::currentDateTimeUtc().secsTo(scheduledAtUtc.toUTC());
        if (!scheduledAtUtc.isValid() || seconds < 10 || seconds > MaximumScheduleSeconds)
        {
            if (error != nullptr)
            {
                *error = QStringLiteral("Telegram server scheduling requires a time from 10 "
                                        "seconds to 367 days in the future.");
            }
            return false;
        }
    }

    if (caption.size() > 1024)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Video captions must contain at most 1024 characters.");
        }
        return false;
    }
    m_pendingSubmission = PendingSubmission{videoId,
                                            channel.trimmed(),
                                            localFilePath,
                                            scheduledAtUtc.toUTC(),
                                            std::max(0, durationSeconds),
                                            publishImmediately,
                                            {},
                                            {},
                                            caption,
                                            false};
    m_submissionTimer.start();
    resolveSubmissionChannel();
    return true;
}

bool TdPublisher::reconcileVideo(const qint64 videoId, const QString& tdChatId,
                                 const QString& tdMessageId, QString* error)
{
    if (!m_ready || tdChatId.isEmpty() || tdMessageId.isEmpty() ||
        m_pendingReconciliations.contains(videoId))
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Telegram confirmation is unavailable or already pending.");
        }
        return false;
    }
    m_trackedMessages.insert(tdChatId + QLatin1Char('/') + tdMessageId, videoId);
    m_pendingReconciliations.insert(videoId, QDateTime::currentDateTimeUtc());
    m_reconciliationTimer.start();
    const QJsonObject request{
        {QStringLiteral("@type"), QStringLiteral("getMessage")},
        {QStringLiteral("chat_id"), idValue(tdChatId)},
        {QStringLiteral("message_id"), idValue(tdMessageId)},
        {QStringLiteral("@extra"), extra(QStringLiteral("reconcile"), videoId)},
    };
    if (!m_client.send(request, error))
    {
        m_pendingReconciliations.remove(videoId);
        return false;
    }
    return true;
}

bool TdPublisher::cancelScheduledVideo(const qint64 videoId, const QString& tdChatId,
                                       const QString& tdMessageId, QString* error)
{
    if (!m_ready)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("The TDLib user account is not authorized.");
        }
        return false;
    }
    if (tdChatId.isEmpty() || tdMessageId.isEmpty())
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("The server-side Telegram message identifier is missing.");
        }
        return false;
    }
    if (m_pendingRemoteVideoIds.contains(videoId))
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("A Telegram operation for this post is already pending.");
        }
        return false;
    }
    QJsonObject request{
        {QStringLiteral("@type"), QStringLiteral("deleteMessages")},
        {QStringLiteral("chat_id"), idValue(tdChatId)},
        {QStringLiteral("message_ids"), QJsonArray{idValue(tdMessageId)}},
        {QStringLiteral("revoke"), true},
        {QStringLiteral("@extra"),
         extra(QStringLiteral("remoteOperation"), videoId, QStringLiteral("cancel"))},
    };
    m_pendingRemoteVideoIds.insert(videoId);
    if (!m_client.send(request, error))
    {
        m_pendingRemoteVideoIds.remove(videoId);
        return false;
    }
    return true;
}

bool TdPublisher::rescheduleVideo(const qint64 videoId, const QString& tdChatId,
                                  const QString& tdMessageId, const QDateTime& scheduledAtUtc,
                                  QString* error)
{
    const qint64 seconds = QDateTime::currentDateTimeUtc().secsTo(scheduledAtUtc.toUTC());
    if (!scheduledAtUtc.isValid() || seconds < 10 || seconds > MaximumScheduleSeconds)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Choose a time from 10 seconds to 367 days in the future.");
        }
        return false;
    }
    return sendRemoteOperation(videoId, QStringLiteral("reschedule"), tdChatId, tdMessageId,
                               schedulingState(scheduledAtUtc), error);
}

bool TdPublisher::publishScheduledVideoNow(const qint64 videoId, const QString& tdChatId,
                                           const QString& tdMessageId, QString* error)
{
    return sendRemoteOperation(videoId, QStringLiteral("publish_now"), tdChatId, tdMessageId,
                               QJsonValue::Null, error);
}

void TdPublisher::handleEvent(const QJsonObject& event)
{
    const QString type = event.value(QStringLiteral("@type")).toString();
    if (type == QStringLiteral("updateAuthorizationState"))
    {
        handleAuthorizationState(event.value(QStringLiteral("authorization_state")).toObject());
        return;
    }

    if (type == QStringLiteral("updateMessageSendSucceeded"))
    {
        const QString oldId = jsonId(event.value(QStringLiteral("old_message_id")));
        const QJsonObject message = event.value(QStringLiteral("message")).toObject();
        if (m_pendingSubmission && m_pendingSubmission->temporaryMessageId == oldId &&
            m_pendingSubmission->tdChatId == jsonId(message.value(QStringLiteral("chat_id"))))
        {
            finishSubmission(message);
        }
        else
        {
            const QString key =
                jsonId(message.value(QStringLiteral("chat_id"))) + QLatin1Char('/') + oldId;
            if (m_trackedMessages.contains(key))
            {
                reportReconciledMessage(m_trackedMessages.take(key), message);
            }
        }
        return;
    }
    if (type == QStringLiteral("updateMessageSendFailed") && m_pendingSubmission.has_value())
    {
        const QString oldId = jsonId(event.value(QStringLiteral("old_message_id")));
        if (m_pendingSubmission->temporaryMessageId == oldId)
        {
            failSubmission(errorText(event.value(QStringLiteral("error")).toObject()));
        }
        return;
    }

    const QJsonObject requestExtra = event.value(QStringLiteral("@extra")).toObject();
    const QString kind = requestExtra.value(QStringLiteral("kind")).toString();
    const qint64 requestVideoId =
        requestExtra.value(QStringLiteral("video_id")).toString().toLongLong();
    if (kind.isEmpty())
    {
        return;
    }
    if (type == QStringLiteral("error"))
    {
        const QString message = errorText(event);
        if (kind == QStringLiteral("submitVideo") || kind == QStringLiteral("resolveChannel"))
        {
            if (m_pendingSubmission && m_pendingSubmission->videoId == requestVideoId)
            {
                const auto retryMatch =
                    QRegularExpression(QStringLiteral("(?:retry after |FLOOD_WAIT_)([0-9]+)"),
                                       QRegularExpression::CaseInsensitiveOption)
                        .match(message);
                const int code = event.value(QStringLiteral("code")).toInt();
                failSubmission(message,
                               m_pendingSubmission->sendStarted && (code < 400 || code >= 500),
                               retryMatch.hasMatch() ? retryMatch.captured(1).toInt() : 0);
            }
        }
        else if (kind == QStringLiteral("reconcile"))
        {
            m_pendingReconciliations.remove(requestVideoId);
            emit videoReconciled(requestVideoId, QStringLiteral("unknown"), {}, {}, {}, message);
        }
        else if (kind == QStringLiteral("remoteOperation"))
        {
            const qint64 videoId =
                requestExtra.value(QStringLiteral("video_id")).toString().toLongLong();
            m_pendingRemoteVideoIds.remove(videoId);
            emit remoteOperationFinished(videoId,
                                         requestExtra.value(QStringLiteral("operation")).toString(),
                                         false, message);
        }
        else if (kind == QStringLiteral("auth"))
        {
            m_statusText = QStringLiteral("Authorization error: %1").arg(message);
            emit authorizationChanged();
            emit activity(QStringLiteral("error"), m_statusText);
        }
        return;
    }

    if (kind == QStringLiteral("reconcile") && type == QStringLiteral("message"))
    {
        reportReconciledMessage(requestVideoId, event);
        return;
    }

    if (kind == QStringLiteral("resolveChannel") && type == QStringLiteral("chat") &&
        m_pendingSubmission.has_value() && m_pendingSubmission->videoId == requestVideoId)
    {
        const QString chatId = jsonId(event.value(QStringLiteral("id")));
        m_chatIds.insert(m_pendingSubmission->channel, chatId);
        sendPendingVideo(chatId);
        return;
    }
    if (kind == QStringLiteral("submitVideo") && type == QStringLiteral("message") &&
        m_pendingSubmission.has_value() && m_pendingSubmission->videoId == requestVideoId)
    {
        const QJsonValue sendingState = event.value(QStringLiteral("sending_state"));
        if (sendingState.isNull() || sendingState.isUndefined())
        {
            finishSubmission(event);
        }
        else
        {
            m_pendingSubmission->temporaryMessageId = jsonId(event.value(QStringLiteral("id")));
        }
        return;
    }
    if (kind == QStringLiteral("remoteOperation") && type == QStringLiteral("ok"))
    {
        const qint64 videoId =
            requestExtra.value(QStringLiteral("video_id")).toString().toLongLong();
        m_pendingRemoteVideoIds.remove(videoId);
        emit remoteOperationFinished(
            videoId, requestExtra.value(QStringLiteral("operation")).toString(), true, {});
    }
}

void TdPublisher::handleAuthorizationState(const QJsonObject& state)
{
    const QString type = state.value(QStringLiteral("@type")).toString();
    if (type == QStringLiteral("authorizationStateWaitTdlibParameters"))
    {
        setAuthorizationState(QStringLiteral("wait_parameters"),
                              QStringLiteral("Configuring the local TDLib database…"), false);
        sendTdlibParameters();
    }
    else if (type == QStringLiteral("authorizationStateWaitPhoneNumber"))
    {
        setAuthorizationState(QStringLiteral("wait_phone"),
                              QStringLiteral("Enter the user account phone number."), false);
        if (!m_config.phoneNumber.trimmed().isEmpty())
        {
            submitPhoneNumber(m_config.phoneNumber);
        }
    }
    else if (type == QStringLiteral("authorizationStateWaitEmailAddress"))
    {
        setAuthorizationState(QStringLiteral("wait_email"),
                              QStringLiteral("Telegram requires an email address."), false);
    }
    else if (type == QStringLiteral("authorizationStateWaitEmailCode"))
    {
        setAuthorizationState(QStringLiteral("wait_email_code"),
                              QStringLiteral("Enter the code sent to the email address."), false);
    }
    else if (type == QStringLiteral("authorizationStateWaitCode"))
    {
        setAuthorizationState(QStringLiteral("wait_code"),
                              QStringLiteral("Enter the login code sent by Telegram."), false);
    }
    else if (type == QStringLiteral("authorizationStateWaitPassword"))
    {
        const QString hint = state.value(QStringLiteral("password_hint")).toString();
        setAuthorizationState(
            QStringLiteral("wait_password"),
            hint.isEmpty()
                ? QStringLiteral("Enter the two-step verification password.")
                : QStringLiteral("Enter the two-step verification password (hint: %1).").arg(hint),
            false);
    }
    else if (type == QStringLiteral("authorizationStateWaitRegistration"))
    {
        setAuthorizationState(QStringLiteral("wait_registration"),
                              QStringLiteral("This phone number needs Telegram registration."),
                              false);
    }
    else if (type == QStringLiteral("authorizationStateWaitOtherDeviceConfirmation"))
    {
        setAuthorizationState(QStringLiteral("wait_other_device"),
                              QStringLiteral("Confirm login from another device: %1")
                                  .arg(state.value(QStringLiteral("link")).toString()),
                              false);
    }
    else if (type == QStringLiteral("authorizationStateReady"))
    {
        setAuthorizationState(QStringLiteral("ready"),
                              QStringLiteral("User account connected. Server scheduling is ready."),
                              true);
        emit activity(QStringLiteral("success"), QStringLiteral("TDLib user account authorized."));
    }
    else if (type == QStringLiteral("authorizationStateLoggingOut"))
    {
        setAuthorizationState(QStringLiteral("logging_out"),
                              QStringLiteral("Signing out of the TDLib account…"), false);
    }
    else if (type == QStringLiteral("authorizationStateClosing"))
    {
        setAuthorizationState(QStringLiteral("closing"), QStringLiteral("TDLib is closing…"),
                              false);
    }
    else if (type == QStringLiteral("authorizationStateClosed"))
    {
        setAuthorizationState(QStringLiteral("closed"), QStringLiteral("TDLib session closed."),
                              false);
    }
    else
    {
        setAuthorizationState(QStringLiteral("unsupported"),
                              QStringLiteral("Unsupported TDLib authorization state: %1").arg(type),
                              false);
    }
}

void TdPublisher::sendTdlibParameters()
{
    const QString filesDirectory =
        QDir(m_config.databaseDirectory).filePath(QStringLiteral("files"));
    QDir().mkpath(filesDirectory);
    QJsonObject request{
        {QStringLiteral("@type"), QStringLiteral("setTdlibParameters")},
        {QStringLiteral("use_test_dc"), false},
        {QStringLiteral("database_directory"), m_config.databaseDirectory},
        {QStringLiteral("files_directory"), filesDirectory},
        {QStringLiteral("database_encryption_key"),
         QString::fromLatin1(m_config.databaseEncryptionKey.toBase64())},
        {QStringLiteral("use_file_database"), true},
        {QStringLiteral("use_chat_info_database"), true},
        {QStringLiteral("use_message_database"), true},
        {QStringLiteral("use_secret_chats"), false},
        {QStringLiteral("api_id"), m_config.apiId},
        {QStringLiteral("api_hash"), m_config.apiHash},
        {QStringLiteral("system_language_code"), QLocale::system().name().left(2)},
        {QStringLiteral("device_model"), QStringLiteral("Desktop PC")},
        {QStringLiteral("system_version"), QSysInfo::prettyProductName()},
        {QStringLiteral("application_version"), QCoreApplication::applicationVersion().isEmpty()
                                                    ? QStringLiteral("2.1.0")
                                                    : QCoreApplication::applicationVersion()},
        {QStringLiteral("@extra"), extra(QStringLiteral("auth"))},
    };
    QString error;
    if (!m_client.send(request, &error))
    {
        setAuthorizationState(QStringLiteral("error"), error, false);
    }
}

void TdPublisher::resolveSubmissionChannel()
{
    if (!m_pendingSubmission.has_value())
    {
        return;
    }
    const QString channel = m_pendingSubmission->channel;
    if (m_chatIds.contains(channel))
    {
        sendPendingVideo(m_chatIds.value(channel));
        return;
    }
    if (channel.startsWith(QLatin1Char('@')))
    {
        QJsonObject request{
            {QStringLiteral("@type"), QStringLiteral("searchPublicChat")},
            {QStringLiteral("username"), channel.mid(1)},
            {QStringLiteral("@extra"),
             extra(QStringLiteral("resolveChannel"), m_pendingSubmission->videoId)},
        };
        QString error;
        if (!m_client.send(request, &error))
        {
            failSubmission(error);
        }
        return;
    }

    bool valid = false;
    static_cast<void>(channel.toLongLong(&valid));
    if (!valid)
    {
        failSubmission(QStringLiteral("Use @channel_username or a numeric TDLib chat ID."));
        return;
    }
    m_chatIds.insert(channel, channel);
    sendPendingVideo(channel);
}

void TdPublisher::sendPendingVideo(const QString& tdChatId)
{
    if (!m_pendingSubmission.has_value())
    {
        return;
    }
    m_pendingSubmission->tdChatId = tdChatId;

    QJsonObject options{
        {QStringLiteral("@type"), QStringLiteral("messageSendOptions")},
        {QStringLiteral("disable_notification"), false},
        {QStringLiteral("from_background"), false},
        {QStringLiteral("protect_content"), false},
        {QStringLiteral("scheduling_state"),
         m_pendingSubmission->publishImmediately
             ? QJsonValue::Null
             : QJsonValue(schedulingState(m_pendingSubmission->scheduledAtUtc))},
    };
    QJsonObject content{
        {QStringLiteral("@type"), QStringLiteral("inputMessageVideo")},
        {QStringLiteral("video"),
         QJsonObject{{QStringLiteral("@type"), QStringLiteral("inputFileLocal")},
                     {QStringLiteral("path"), m_pendingSubmission->localFilePath}}},
        {QStringLiteral("thumbnail"), QJsonValue::Null},
        {QStringLiteral("cover"), QJsonValue::Null},
        {QStringLiteral("start_timestamp"), 0},
        {QStringLiteral("added_sticker_file_ids"), QJsonArray{}},
        {QStringLiteral("duration"), m_pendingSubmission->durationSeconds},
        {QStringLiteral("width"), 0},
        {QStringLiteral("height"), 0},
        {QStringLiteral("supports_streaming"), true},
        {QStringLiteral("caption"),
         QJsonObject{{QStringLiteral("@type"), QStringLiteral("formattedText")},
                     {QStringLiteral("text"), m_pendingSubmission->caption},
                     {QStringLiteral("entities"), QJsonArray{}}}},
        {QStringLiteral("show_caption_above_media"), false},
        {QStringLiteral("self_destruct_type"), QJsonValue::Null},
        {QStringLiteral("has_spoiler"), false},
    };
    QJsonObject request{
        {QStringLiteral("@type"), QStringLiteral("sendMessage")},
        {QStringLiteral("chat_id"), idValue(tdChatId)},
        {QStringLiteral("topic_id"), QJsonValue::Null},
        {QStringLiteral("reply_to"), QJsonValue::Null},
        {QStringLiteral("options"), options},
        {QStringLiteral("reply_markup"), QJsonValue::Null},
        {QStringLiteral("input_message_content"), content},
        {QStringLiteral("@extra"),
         extra(QStringLiteral("submitVideo"), m_pendingSubmission->videoId)},
    };
    QString error;
    if (!m_client.send(request, &error))
    {
        failSubmission(error);
    }
    else if (m_pendingSubmission)
    {
        m_pendingSubmission->sendStarted = true;
    }
}

void TdPublisher::finishSubmission(const QJsonObject& message)
{
    if (!m_pendingSubmission.has_value())
    {
        return;
    }
    const PendingSubmission submitted = *m_pendingSubmission;
    const QString chatId = jsonId(message.value(QStringLiteral("chat_id")));
    const QString messageId = jsonId(message.value(QStringLiteral("id")));
    if (messageId.isEmpty() || messageId == QStringLiteral("0"))
    {
        failSubmission(QStringLiteral("Telegram returned no usable message identifier."), true);
        return;
    }
    const bool scheduled = !message.value(QStringLiteral("scheduling_state")).isNull() &&
                           !message.value(QStringLiteral("scheduling_state")).isUndefined();
    m_submissionTimer.stop();
    m_trackedMessages.insert((chatId.isEmpty() ? submitted.tdChatId : chatId) + QLatin1Char('/') +
                                 messageId,
                             submitted.videoId);
    m_pendingSubmission.reset();
    emit videoAccepted(submitted.videoId, chatId.isEmpty() ? submitted.tdChatId : chatId, messageId,
                       scheduled);
}

void TdPublisher::failSubmission(const QString& error, const bool unknown,
                                 const int retryAfterSeconds)
{
    if (!m_pendingSubmission.has_value())
    {
        return;
    }
    const qint64 videoId = m_pendingSubmission->videoId;
    m_submissionTimer.stop();
    m_pendingSubmission.reset();
    emit videoFailureDetailed(videoId, error, unknown, retryAfterSeconds);
    emit videoFailed(videoId, error.isEmpty() ? QStringLiteral("TDLib request failed.") : error);
}

void TdPublisher::reportReconciledMessage(const qint64 videoId, const QJsonObject& message)
{
    m_pendingReconciliations.remove(videoId);
    const QString chatId = jsonId(message.value(QStringLiteral("chat_id")));
    const QString messageId = jsonId(message.value(QStringLiteral("id")));
    const QJsonValue sendingState = message.value(QStringLiteral("sending_state"));
    if (chatId.isEmpty() || messageId.isEmpty() || messageId == QStringLiteral("0") ||
        (!sendingState.isNull() && !sendingState.isUndefined()))
    {
        emit videoReconciled(videoId, QStringLiteral("unknown"), chatId, messageId, {},
                             QStringLiteral("Telegram has not confirmed the message delivery."));
        return;
    }
    const QJsonValue schedule = message.value(QStringLiteral("scheduling_state"));
    const bool scheduled = !schedule.isNull() && !schedule.isUndefined();
    m_trackedMessages.insert(chatId + QLatin1Char('/') + messageId, videoId);
    const qint64 timestamp = message.value(QStringLiteral("date")).toInteger();
    emit videoReconciled(videoId,
                         scheduled ? QStringLiteral("scheduled") : QStringLiteral("published"),
                         chatId, messageId,
                         timestamp > 0 ? QDateTime::fromSecsSinceEpoch(timestamp, QTimeZone::UTC)
                                       : QDateTime::currentDateTimeUtc(),
                         {});
}

void TdPublisher::sendAuthenticationRequest(QJsonObject request)
{
    request.insert(QStringLiteral("@extra"), extra(QStringLiteral("auth")));
    QString error;
    if (!m_client.send(request, &error))
    {
        m_statusText = error;
        emit authorizationChanged();
    }
}

bool TdPublisher::sendRemoteOperation(const qint64 videoId, const QString& operation,
                                      const QString& tdChatId, const QString& tdMessageId,
                                      const QJsonValue& newSchedulingState, QString* error)
{
    if (!m_ready)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("The TDLib user account is not authorized.");
        }
        return false;
    }
    if (m_pendingRemoteVideoIds.contains(videoId))
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("A Telegram operation for this post is already pending.");
        }
        return false;
    }
    if (tdChatId.isEmpty() || tdMessageId.isEmpty())
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("The server-side Telegram message identifier is missing.");
        }
        return false;
    }
    QJsonObject request{
        {QStringLiteral("@type"), QStringLiteral("editMessageSchedulingState")},
        {QStringLiteral("chat_id"), idValue(tdChatId)},
        {QStringLiteral("message_id"), idValue(tdMessageId)},
        {QStringLiteral("scheduling_state"), newSchedulingState},
        {QStringLiteral("@extra"), extra(QStringLiteral("remoteOperation"), videoId, operation)},
    };
    m_pendingRemoteVideoIds.insert(videoId);
    if (!m_client.send(request, error))
    {
        m_pendingRemoteVideoIds.remove(videoId);
        return false;
    }
    return true;
}

void TdPublisher::setAuthorizationState(QString state, QString status, const bool ready)
{
    const bool readyWasChanged = m_ready != ready;
    const bool stateWasChanged = m_authorizationState != state || m_statusText != status;
    m_authorizationState = std::move(state);
    m_statusText = std::move(status);
    m_ready = ready;
    if (stateWasChanged)
    {
        emit authorizationChanged();
    }
    if (readyWasChanged)
    {
        emit readyChanged(m_ready);
    }
}

QString TdPublisher::jsonId(const QJsonValue& value)
{
    if (value.isString())
    {
        return value.toString();
    }
    if (value.isDouble())
    {
        const auto number = value.toInteger();
        return number != 0 ? QString::number(number) : QString{};
    }
    return {};
}

QJsonValue TdPublisher::idValue(const QString& value)
{
    bool valid = false;
    const qint64 number = value.toLongLong(&valid);
    return valid ? QJsonValue(number) : QJsonValue(value);
}

QString TdPublisher::errorText(const QJsonObject& error)
{
    const int code = error.value(QStringLiteral("code")).toInt();
    const QString message = error.value(QStringLiteral("message")).toString();
    return code > 0 ? QStringLiteral("%1 (code %2)").arg(message).arg(code) : message;
}
