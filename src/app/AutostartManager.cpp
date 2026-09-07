/******************************************************************************
 * @file    AutostartManager.cpp
 * @brief   Implements Windows user-level application autostart management.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#include "app/AutostartManager.h"

#include <QCoreApplication>
#include <QSettings>

namespace
{

constexpr auto RunRegistryPath =
    "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr auto RunEntryName = "TikTokTelegramBot";

QString expectedCommand()
{
    return QStringLiteral("\"%1\" --minimized").arg(QCoreApplication::applicationFilePath());
}

} // namespace

bool AutostartManager::isEnabled()
{
#ifdef Q_OS_WIN
    QSettings registry(QString::fromLatin1(RunRegistryPath), QSettings::NativeFormat);
    return registry.value(QString::fromLatin1(RunEntryName)).toString() == expectedCommand();
#else
    return false;
#endif
}

bool AutostartManager::setEnabled(const bool enabled, QString* error)
{
#ifdef Q_OS_WIN
    QSettings registry(QString::fromLatin1(RunRegistryPath), QSettings::NativeFormat);
    if (enabled)
    {
        registry.setValue(QString::fromLatin1(RunEntryName), expectedCommand());
    }
    else
    {
        registry.remove(QString::fromLatin1(RunEntryName));
    }
    registry.sync();
    if (registry.status() != QSettings::NoError)
    {
        if (error != nullptr)
        {
            *error = QStringLiteral("Windows autostart registry error.");
        }
        return false;
    }
    return true;
#else
    Q_UNUSED(enabled);
    if (error != nullptr)
    {
        *error = QStringLiteral("Autostart is currently implemented for Windows only.");
    }
    return false;
#endif
}
