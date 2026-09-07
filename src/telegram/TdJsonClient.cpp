/******************************************************************************
 * @file    TdJsonClient.cpp
 * @brief   Implements dynamic loading and asynchronous receive for TDLib JSON.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#include "telegram/TdJsonClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>

#include <utility>

namespace
{

template <typename Function> Function resolveFunction(QLibrary& library, const char* name)
{
    return reinterpret_cast<Function>(library.resolve(name));
}

QStringList libraryCandidates(const QString& configuredPath)
{
    QStringList candidates;
    if (!configuredPath.trimmed().isEmpty())
    {
        candidates.append(QDir::cleanPath(configuredPath.trimmed()));
    }
    candidates.append(
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("tdjson.dll")));
    candidates.append(QStringLiteral("tdjson"));
    candidates.removeDuplicates();
    return candidates;
}

} // namespace

TdJsonClient::TdJsonClient(QObject* parent)
    : QObject(parent)
{
}

TdJsonClient::~TdJsonClient()
{
    stop();
}

bool TdJsonClient::start(const QString& libraryPath, QString* error)
{
    if (m_running.load())
    {
        return true;
    }
    if (!loadLibrary(libraryPath, error))
    {
        return false;
    }

    m_clientId = m_createClientId();
    if (m_clientId <= 0)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("TDLib did not create a client identifier.");
        }
        return false;
    }

    m_running.store(true);
    m_receiveThread = std::jthread([this](const std::stop_token token) { receiveLoop(token); });
    return true;
}

void TdJsonClient::stop()
{
    if (!m_running.exchange(false))
    {
        return;
    }

    if (m_send != nullptr && m_clientId > 0)
    {
        const QByteArray closeRequest = QByteArrayLiteral("{\"@type\":\"close\"}");
        m_send(m_clientId, closeRequest.constData());
    }
    if (m_receiveThread.joinable())
    {
        m_receiveThread.request_stop();
        m_receiveThread.join();
    }
    m_clientId = 0;
}

bool TdJsonClient::isRunning() const noexcept
{
    return m_running.load();
}

QString TdJsonClient::loadedLibraryPath() const
{
    return m_library.fileName();
}

bool TdJsonClient::send(const QJsonObject& request, QString* error) const
{
    if (!m_running.load() || m_send == nullptr || m_clientId <= 0)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("TDLib is not running.");
        }
        return false;
    }
    const QByteArray json = QJsonDocument(request).toJson(QJsonDocument::Compact);
    m_send(m_clientId, json.constData());
    return true;
}

bool TdJsonClient::loadLibrary(const QString& libraryPath, QString* error)
{
    QStringList errors;
    for (const QString& candidate : libraryCandidates(libraryPath))
    {
        m_library.setFileName(candidate);
        m_library.setLoadHints(QLibrary::ResolveAllSymbolsHint | QLibrary::PreventUnloadHint);
        if (!m_library.load())
        {
            errors.append(QStringLiteral("%1: %2").arg(candidate, m_library.errorString()));
            continue;
        }

        m_createClientId =
            resolveFunction<CreateClientIdFunction>(m_library, "td_create_client_id");
        m_send = resolveFunction<SendFunction>(m_library, "td_send");
        m_receive = resolveFunction<ReceiveFunction>(m_library, "td_receive");
        m_execute = resolveFunction<ExecuteFunction>(m_library, "td_execute");
        if (m_createClientId != nullptr && m_send != nullptr && m_receive != nullptr &&
            m_execute != nullptr)
        {
            return true;
        }

        errors.append(QStringLiteral("%1: incompatible tdjson library").arg(candidate));
        m_createClientId = nullptr;
        m_send = nullptr;
        m_receive = nullptr;
        m_execute = nullptr;
        // PreventUnloadHint protects against TDLib worker threads surviving a close.
        // If a file loaded but lacks the current API, it cannot safely be replaced in
        // this process; report the incompatible binary immediately.
        break;
    }

    if (error != nullptr)
    {
        *error = QStringLiteral(
                     "Could not load TDLib. Select tdjson.dll built for this architecture. %1")
                     .arg(errors.join(QStringLiteral(" | ")));
    }
    return false;
}

void TdJsonClient::receiveLoop(const std::stop_token stopToken)
{
    while (!stopToken.stop_requested() && m_running.load())
    {
        const char* received = m_receive(0.25);
        if (received == nullptr)
        {
            continue;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(QByteArray(received), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
        {
            emit receiveError(QStringLiteral("Invalid JSON received from TDLib: %1")
                                  .arg(parseError.errorString()));
            continue;
        }
        emit eventReceived(document.object());
    }
}
