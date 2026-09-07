/******************************************************************************
 * @file    TdPublisher.h
 * @brief   Declares user authorization and Telegram server scheduling through TDLib.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#ifndef TIKTOK_TELEGRAM_BOT_TELEGRAM_TD_PUBLISHER_H
#define TIKTOK_TELEGRAM_BOT_TELEGRAM_TD_PUBLISHER_H

#include "telegram/TdJsonClient.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

#include <optional>

/** @brief Runtime configuration required to initialize one TDLib user session. */
struct TdPublisherConfig
{
    QString libraryPath;              /**< Explicit `tdjson` path, or empty for lookup. */
    int apiId{};                      /**< Telegram application identifier. */
    QString apiHash;                  /**< Secret Telegram application hash. */
    QString phoneNumber;              /**< User account phone number in international form. */
    QByteArray databaseEncryptionKey; /**< Key for the local TDLib database. */
    QString databaseDirectory;        /**< Private directory for TDLib state. */
};

/**
 * @brief Authorizes a Telegram user and manages native scheduled video messages.
 *
 * Only one video submission is uploaded at a time. Login secrets are accepted through
 * transient methods and are never persisted by this class.
 */
class TdPublisher final : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Constructs an inactive Telegram server publisher.
     *
     * @param[in] parent Parent object that owns this instance; may be nullptr.
     */
    explicit TdPublisher(QObject* parent = nullptr);
    /** @brief Stops TDLib receive processing. */
    ~TdPublisher() override;

    /**
     * @brief Starts TDLib authorization with the supplied configuration.
     *
     * @param[in] config Valid TDLib runtime and credential configuration.
     * @param[out] error Receives a startup diagnostic; may be nullptr.
     * @return `true` when TDLib started and authorization can proceed.
     */
    [[nodiscard]] bool start(TdPublisherConfig config, QString* error = nullptr);
    /** @brief Stops the local client without logging out the Telegram session. */
    void stop();
    /** @brief Requests explicit logout and removal of active Telegram authorization. */
    void logOut();

    /** @return `true` when TDLib reports `authorizationStateReady`. */
    [[nodiscard]] bool isReady() const noexcept;
    /** @return `true` while a video submission or remote operation is pending. */
    [[nodiscard]] bool isBusy() const noexcept;
    /** @return Stable machine-readable authorization state for the QML form. */
    [[nodiscard]] QString authorizationState() const;
    /** @return User-facing authorization or connection status. */
    [[nodiscard]] QString statusText() const;

    /**
     * @brief Submits the account phone number requested by TDLib.
     * @param[in] phoneNumber Account number in international form.
     */
    void submitPhoneNumber(const QString& phoneNumber);
    /**
     * @brief Submits a one-time Telegram authorization code.
     * @param[in] code Transient authorization code.
     */
    void submitAuthenticationCode(const QString& code);
    /**
     * @brief Submits a transient two-step-verification password.
     * @param[in] password Account password; never persisted by this class.
     */
    void submitPassword(const QString& password);
    /**
     * @brief Submits an email address requested by Telegram authorization.
     * @param[in] emailAddress Account recovery or verification email address.
     */
    void submitEmailAddress(const QString& emailAddress);
    /**
     * @brief Submits a one-time email verification code.
     * @param[in] code Transient email code.
     */
    void submitEmailCode(const QString& code);
    /**
     * @brief Submits names required when Telegram creates a new account.
     * @param[in] firstName Required account first name.
     * @param[in] lastName Optional account last name.
     */
    void submitRegistration(const QString& firstName, const QString& lastName);

    /**
     * @brief Uploads a local video as an immediate or server-scheduled channel post.
     *
     * @param[in] videoId Local catalog identifier used for asynchronous correlation.
     * @param[in] channel Destination username or TDLib chat identifier.
     * @param[in] localFilePath Local media file owned until Telegram accepts it.
     * @param[in] scheduledAtUtc Future server publication time in UTC.
     * @param[in] durationSeconds Video duration supplied to TDLib.
     * @param[in] publishImmediately Whether to omit the scheduling state.
     * @param[out] error Receives a validation or transport diagnostic; may be nullptr.
     * @param[in] caption Optional Telegram caption containing at most 1024 characters.
     * @return `true` when asynchronous submission began; completion arrives by signal.
     */
    [[nodiscard]] bool submitVideo(qint64 videoId, const QString& channel,
                                   const QString& localFilePath, const QDateTime& scheduledAtUtc,
                                   int durationSeconds, bool publishImmediately,
                                   QString* error = nullptr, const QString& caption = {});
    /** @brief Queries Telegram for the stored message; absence never proves publication. */
    [[nodiscard]] bool reconcileVideo(qint64 videoId, const QString& tdChatId,
                                      const QString& tdMessageId, QString* error = nullptr);
    /**
     * @brief Deletes a server-scheduled message for the catalog video.
     * @param[in] videoId Local catalog identifier.
     * @param[in] tdChatId Remote TDLib chat identifier.
     * @param[in] tdMessageId Remote TDLib message identifier.
     * @param[out] error Receives a diagnostic; may be nullptr.
     * @return true when asynchronous deletion begins; otherwise false.
     */
    [[nodiscard]] bool cancelScheduledVideo(qint64 videoId, const QString& tdChatId,
                                            const QString& tdMessageId, QString* error = nullptr);
    /**
     * @brief Changes the Telegram scheduling state to a new UTC date.
     * @param[in] videoId Local catalog identifier.
     * @param[in] tdChatId Remote TDLib chat identifier.
     * @param[in] tdMessageId Remote TDLib message identifier.
     * @param[in] scheduledAtUtc New valid future UTC timestamp.
     * @param[out] error Receives a diagnostic; may be nullptr.
     * @return true when asynchronous rescheduling begins; otherwise false.
     */
    [[nodiscard]] bool rescheduleVideo(qint64 videoId, const QString& tdChatId,
                                       const QString& tdMessageId, const QDateTime& scheduledAtUtc,
                                       QString* error = nullptr);
    /**
     * @brief Clears scheduling state so Telegram publishes immediately.
     * @param[in] videoId Local catalog identifier.
     * @param[in] tdChatId Remote TDLib chat identifier.
     * @param[in] tdMessageId Remote TDLib message identifier.
     * @param[out] error Receives a diagnostic; may be nullptr.
     * @return true when the asynchronous operation begins; otherwise false.
     */
    [[nodiscard]] bool publishScheduledVideoNow(qint64 videoId, const QString& tdChatId,
                                                const QString& tdMessageId,
                                                QString* error = nullptr);

signals:
    /** @brief Emitted whenever authorization state or status text changes. */
    void authorizationChanged();
    /**
     * @brief Reports whether TDLib accepts publication requests.
     * @param[in] ready Whether TDLib is authorized.
     */
    void readyChanged(bool ready);
    /**
     * @brief Emits a sanitized activity-log entry.
     * @param[in] level Log severity name.
     * @param[in] message Human-readable event text.
     */
    void activity(const QString& level, const QString& message);
    /**
     * @brief Reports that Telegram accepted a video and assigned identifiers.
     * @param[in] videoId Local catalog identifier.
     * @param[in] tdChatId Remote TDLib chat identifier.
     * @param[in] tdMessageId Remote TDLib message identifier.
     * @param[in] scheduled Whether Telegram retained the message for future publication.
     */
    void videoAccepted(qint64 videoId, const QString& tdChatId, const QString& tdMessageId,
                       bool scheduled);
    /**
     * @brief Reports terminal failure of a video submission.
     * @param[in] videoId Local catalog identifier.
     * @param[in] error Sanitized diagnostic.
     */
    void videoFailed(qint64 videoId, const QString& error);
    /** @brief Reports upload uncertainty separately from a definite rejection. */
    void videoFailureDetailed(qint64 videoId, const QString& error, bool deliveryUnknown,
                              int retryAfterSeconds);
    /** @brief Reports a confirmed scheduled/published message or an unknown outcome. */
    void videoReconciled(qint64 videoId, const QString& state, const QString& tdChatId,
                         const QString& tdMessageId, const QDateTime& publishedAtUtc,
                         const QString& error);
    /**
     * @brief Reports completion of cancel, reschedule, or publish-now.
     * @param[in] videoId Local catalog identifier.
     * @param[in] operation Stable operation name.
     * @param[in] success Whether Telegram accepted the operation.
     * @param[in] error Sanitized failure diagnostic, or empty on success.
     */
    void remoteOperationFinished(qint64 videoId, const QString& operation, bool success,
                                 const QString& error);

private slots:
    void handleEvent(const QJsonObject& event);

private:
    struct PendingSubmission
    {
        qint64 videoId{};
        QString channel;
        QString localFilePath;
        QDateTime scheduledAtUtc;
        int durationSeconds{};
        bool publishImmediately{false};
        QString tdChatId;
        QString temporaryMessageId;
        QString caption;
        bool sendStarted{false};
    };

    void handleAuthorizationState(const QJsonObject& state);
    void sendTdlibParameters();
    void resolveSubmissionChannel();
    void sendPendingVideo(const QString& tdChatId);
    void finishSubmission(const QJsonObject& message);
    void failSubmission(const QString& error, bool unknown = false, int retryAfterSeconds = 0);
    void reportReconciledMessage(qint64 videoId, const QJsonObject& message);
    void sendAuthenticationRequest(QJsonObject request);
    [[nodiscard]] bool sendRemoteOperation(qint64 videoId, const QString& operation,
                                           const QString& tdChatId, const QString& tdMessageId,
                                           const QJsonValue& schedulingState, QString* error);
    void setAuthorizationState(QString state, QString status, bool ready);
    [[nodiscard]] static QString jsonId(const QJsonValue& value);
    [[nodiscard]] static QJsonValue idValue(const QString& value);
    [[nodiscard]] static QString errorText(const QJsonObject& error);

    TdJsonClient m_client;
    TdPublisherConfig m_config;
    QHash<QString, QString> m_chatIds;
    QSet<qint64> m_pendingRemoteVideoIds;
    QHash<qint64, QDateTime> m_pendingReconciliations;
    QHash<QString, qint64> m_trackedMessages;
    QTimer m_submissionTimer;
    QTimer m_reconciliationTimer;
    std::optional<PendingSubmission> m_pendingSubmission;
    QString m_authorizationState{QStringLiteral("stopped")};
    QString m_statusText{QStringLiteral("TDLib is stopped.")};
    bool m_ready{false};
};

#endif // TIKTOK_TELEGRAM_BOT_TELEGRAM_TD_PUBLISHER_H
