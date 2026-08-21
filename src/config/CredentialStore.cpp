#include "config/CredentialStore.h"

#include <QByteArray>
#include <QSettings>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincred.h>
#endif

namespace {

constexpr auto CredentialTarget = L"TikTokTelegramBot/TelegramBotToken";
constexpr auto FallbackSettingsKey = "secrets/telegramBotToken";

#ifdef Q_OS_WIN
QString windowsCredentialError(const DWORD errorCode)
{
    return QStringLiteral("Windows Credential Manager error %1").arg(errorCode);
}
#endif

} // namespace

QString CredentialStore::readTelegramBotToken(QString *error)
{
#ifdef Q_OS_WIN
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(CredentialTarget, CRED_TYPE_GENERIC, 0, &credential)) {
        const DWORD errorCode = GetLastError();
        if (errorCode != ERROR_NOT_FOUND && error != nullptr) {
            *error = windowsCredentialError(errorCode);
        }
        return {};
    }

    const QByteArray tokenBytes(
        reinterpret_cast<const char *>(credential->CredentialBlob),
        static_cast<qsizetype>(credential->CredentialBlobSize));
    const QString token = QString::fromUtf8(tokenBytes);
    CredFree(credential);
    return token;
#else
    Q_UNUSED(error);
    QSettings settings;
    return settings.value(QString::fromLatin1(FallbackSettingsKey)).toString();
#endif
}

bool CredentialStore::writeTelegramBotToken(const QString &token, QString *error)
{
#ifdef Q_OS_WIN
    const QByteArray tokenBytes = token.toUtf8();
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(CredentialTarget);
    credential.CredentialBlobSize = static_cast<DWORD>(tokenBytes.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(
        const_cast<char *>(tokenBytes.constData()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<LPWSTR>(L"TelegramBot");

    if (!CredWriteW(&credential, 0)) {
        if (error != nullptr) {
            *error = windowsCredentialError(GetLastError());
        }
        return false;
    }
    return true;
#else
    Q_UNUSED(error);
    QSettings settings;
    settings.setValue(QString::fromLatin1(FallbackSettingsKey), token);
    settings.sync();
    return settings.status() == QSettings::NoError;
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
