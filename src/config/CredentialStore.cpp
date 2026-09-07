/******************************************************************************
 * @file    CredentialStore.cpp
 * @brief   Implements protected persistence for application credentials.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#include "config/CredentialStore.h"

#include <QByteArray>
#include <QSettings>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
// clang-format off: wincred.h depends on declarations from windows.h.
#include <windows.h>
#include <wincred.h>
// clang-format on
#endif

namespace
{

constexpr auto BotTokenCredentialTarget = L"TikTokTelegramBot/TelegramBotToken";
constexpr auto TdApiHashCredentialTarget = L"TikTokTelegramBot/TdApiHash";
constexpr auto TdDatabaseKeyCredentialTarget = L"TikTokTelegramBot/TdDatabaseKey";
constexpr auto BotTokenFallbackSettingsKey = "secrets/telegramBotToken";
constexpr auto TdApiHashFallbackSettingsKey = "secrets/tdApiHash";
constexpr auto TdDatabaseKeyFallbackSettingsKey = "secrets/tdDatabaseKey";

#ifdef Q_OS_WIN
QString windowsCredentialError(const DWORD errorCode)
{
    return QStringLiteral("Windows Credential Manager error %1").arg(errorCode);
}

QString readWindowsCredential(const wchar_t* target, QString* error)
{
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(target, CRED_TYPE_GENERIC, 0, &credential))
    {
        const DWORD errorCode = GetLastError();
        if (errorCode != ERROR_NOT_FOUND && error != nullptr)
        {
            *error = windowsCredentialError(errorCode);
        }
        return {};
    }
    const QByteArray bytes(reinterpret_cast<const char*>(credential->CredentialBlob),
                           static_cast<qsizetype>(credential->CredentialBlobSize));
    const QString value = QString::fromUtf8(bytes);
    CredFree(credential);
    return value;
}

bool writeWindowsCredential(const wchar_t* target, const wchar_t* userName, const QString& value,
                            QString* error)
{
    const QByteArray bytes = value.toUtf8();
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(target);
    credential.CredentialBlobSize = static_cast<DWORD>(bytes.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(bytes.constData()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<LPWSTR>(userName);
    if (!CredWriteW(&credential, 0))
    {
        if (error != nullptr)
        {
            *error = windowsCredentialError(GetLastError());
        }
        return false;
    }
    return true;
}
#endif

QString readFallback(const char* key)
{
    QSettings settings;
    return settings.value(QString::fromLatin1(key)).toString();
}

bool writeFallback(const char* key, const QString& value)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(key), value);
    settings.sync();
    return settings.status() == QSettings::NoError;
}

} // namespace

QString CredentialStore::readTelegramBotToken(QString* error)
{
#ifdef Q_OS_WIN
    return readWindowsCredential(BotTokenCredentialTarget, error);
#else
    Q_UNUSED(error);
    return readFallback(BotTokenFallbackSettingsKey);
#endif
}

bool CredentialStore::writeTelegramBotToken(const QString& token, QString* error)
{
#ifdef Q_OS_WIN
    return writeWindowsCredential(BotTokenCredentialTarget, L"TelegramBot", token, error);
#else
    Q_UNUSED(error);
    return writeFallback(BotTokenFallbackSettingsKey, token);
#endif
}

QString CredentialStore::readTdApiHash(QString* error)
{
#ifdef Q_OS_WIN
    return readWindowsCredential(TdApiHashCredentialTarget, error);
#else
    Q_UNUSED(error);
    return readFallback(TdApiHashFallbackSettingsKey);
#endif
}

bool CredentialStore::writeTdApiHash(const QString& apiHash, QString* error)
{
#ifdef Q_OS_WIN
    return writeWindowsCredential(TdApiHashCredentialTarget, L"TelegramUser", apiHash, error);
#else
    Q_UNUSED(error);
    return writeFallback(TdApiHashFallbackSettingsKey, apiHash);
#endif
}

QByteArray CredentialStore::readTdDatabaseEncryptionKey(QString* error)
{
#ifdef Q_OS_WIN
    const QString encoded = readWindowsCredential(TdDatabaseKeyCredentialTarget, error);
#else
    Q_UNUSED(error);
    const QString encoded = readFallback(TdDatabaseKeyFallbackSettingsKey);
#endif
    return QByteArray::fromBase64(encoded.toLatin1());
}

bool CredentialStore::writeTdDatabaseEncryptionKey(const QByteArray& key, QString* error)
{
    const QString encoded = QString::fromLatin1(key.toBase64());
#ifdef Q_OS_WIN
    return writeWindowsCredential(TdDatabaseKeyCredentialTarget, L"TelegramUser", encoded, error);
#else
    Q_UNUSED(error);
    return writeFallback(TdDatabaseKeyFallbackSettingsKey, encoded);
#endif
}

bool CredentialStore::usesSecureStorage() noexcept
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

QString CredentialStore::storageDescription()
{
#ifdef Q_OS_WIN
    return QStringLiteral("Windows Credential Manager");
#else
    return QStringLiteral("local application settings (not encrypted)");
#endif
}
