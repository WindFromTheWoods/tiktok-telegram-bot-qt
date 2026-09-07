/******************************************************************************
 * @file    AutostartManager.h
 * @brief   Provides platform-specific application autostart management.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#ifndef TIKTOK_TELEGRAM_BOT_APP_AUTOSTART_MANAGER_H
#define TIKTOK_TELEGRAM_BOT_APP_AUTOSTART_MANAGER_H

#include <QString>

/** @brief Manages the current user's Windows autostart entry. */
class AutostartManager final
{
public:
    /**
     * @brief Determines whether the expected autostart command is registered.
     * @return `true` when the exact current executable command is registered.
     */
    [[nodiscard]] static bool isEnabled();

    /**
     * @brief Adds or removes the application from user autostart.
     *
     * @param[in] enabled `true` to register the application; `false` to remove it.
     * @param[out] error Receives a diagnostic when the operation fails; may be nullptr.
     * @return `true` when the platform setting was updated successfully.
     */
    [[nodiscard]] static bool setEnabled(bool enabled, QString* error = nullptr);
};

#endif // TIKTOK_TELEGRAM_BOT_APP_AUTOSTART_MANAGER_H
