/******************************************************************************
 * @file    TdJsonClient.h
 * @brief   Declares a dynamically loaded, asynchronous TDLib JSON transport.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#ifndef TIKTOK_TELEGRAM_BOT_TELEGRAM_TD_JSON_CLIENT_H
#define TIKTOK_TELEGRAM_BOT_TELEGRAM_TD_JSON_CLIENT_H

#include <QJsonObject>
#include <QLibrary>
#include <QObject>
#include <QString>

#include <atomic>
#include <thread>

/**
 * @brief Loads `tdjson` and receives events on a dedicated worker thread.
 *
 * Public methods belong to the object's Qt thread. Parsed events cross back through
 * queued Qt signals; callers never access TDLib's transient receive buffer directly.
 */
class TdJsonClient final : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Constructs an inactive TDLib JSON transport.
     *
     * @param[in] parent Parent object that owns this instance; may be nullptr.
     */
    explicit TdJsonClient(QObject* parent = nullptr);
    /** @brief Stops the receive worker before releasing process resources. */
    ~TdJsonClient() override;

    TdJsonClient(const TdJsonClient&) = delete;
    TdJsonClient& operator=(const TdJsonClient&) = delete;

    /**
     * @brief Loads TDLib, resolves its C API, and starts receiving.
     *
     * @param[in] libraryPath Explicit `tdjson` path, or empty for platform lookup.
     * @param[out] error Receives an actionable load diagnostic; may be nullptr.
     * @return `true` when the receive worker was started.
     */
    [[nodiscard]] bool start(const QString& libraryPath, QString* error = nullptr);
    /** @brief Requests worker termination and waits for it to finish. */
    void stop();
    /** @return `true` while the receive worker accepts requests. */
    [[nodiscard]] bool isRunning() const noexcept;
    /** @return The resolved dynamic-library path, when loaded. */
    [[nodiscard]] QString loadedLibraryPath() const;
    /**
     * @brief Serializes and sends one TDLib JSON request.
     *
     * @param[in] request Request object containing an `@type` field.
     * @param[out] error Receives a transport diagnostic; may be nullptr.
     * @return `true` when the request was handed to TDLib.
     */
    [[nodiscard]] bool send(const QJsonObject& request, QString* error = nullptr) const;

signals:
    /**
     * @brief Delivers a parsed TDLib update or response on the QObject thread.
     * @param[in] event Parsed TDLib JSON object.
     */
    void eventReceived(const QJsonObject& event);
    /**
     * @brief Reports a receive or JSON parsing failure.
     * @param[in] error Sanitized diagnostic.
     */
    void receiveError(const QString& error);

private:
    using CreateClientIdFunction = int (*)();
    using SendFunction = void (*)(int, const char*);
    using ReceiveFunction = const char* (*)(double);
    using ExecuteFunction = const char* (*)(const char*);

    [[nodiscard]] bool loadLibrary(const QString& libraryPath, QString* error);
    void receiveLoop(std::stop_token stopToken);

    QLibrary m_library;
    CreateClientIdFunction m_createClientId{};
    SendFunction m_send{};
    ReceiveFunction m_receive{};
    ExecuteFunction m_execute{};
    std::jthread m_receiveThread;
    std::atomic_bool m_running{false};
    int m_clientId{};
};

#endif // TIKTOK_TELEGRAM_BOT_TELEGRAM_TD_JSON_CLIENT_H
