/******************************************************************************
 * @file    TrayController.h
 * @brief   Declares system-tray integration and close-to-tray behavior.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#ifndef TIKTOK_TELEGRAM_BOT_UI_TRAY_CONTROLLER_H
#define TIKTOK_TELEGRAM_BOT_UI_TRAY_CONTROLLER_H

#include <QObject>

class QAction;
class QMenu;
class QSystemTrayIcon;
class QWindow;

/**
 * @brief Owns the tray icon and maps window close events to hide behavior.
 *
 * The supplied window is non-owning and must outlive this controller.
 */
class TrayController final : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Constructs tray actions for the application window.
     *
     * @param[in] window Non-null application window that outlives this object.
     * @param[in] parent Parent object that owns this instance; may be nullptr.
     */
    explicit TrayController(QWindow* window, QObject* parent = nullptr);
    /** @brief Removes the event filter and destroys tray-owned resources. */
    ~TrayController() override;

    /**
     * @brief Updates enabled actions and icon state for the bot lifecycle.
     * @param[in] running Whether the bot workflow is running.
     */
    void setBotRunning(bool running);
    /**
     * @brief Displays a native tray notification when supported.
     * @param[in] title Notification title.
     * @param[in] message Notification body.
     */
    void showNotification(const QString& title, const QString& message);
    /** @brief Shows, raises, and activates the application window. */
    void showWindow();
    /** @brief Hides the application window while leaving the process running. */
    void hideWindow();

signals:
    /** @brief Requests that the settings facade start the bot. */
    void startRequested();
    /** @brief Requests that the settings facade stop the bot. */
    void stopRequested();
    /** @brief Requests a clean application exit. */
    void quitRequested();

protected:
    /**
     * @brief Intercepts window close events to implement close-to-tray behavior.
     * @param[in] watched Object that received the event.
     * @param[in] event Event offered to this filter.
     * @return true when the event was consumed; otherwise false.
     */
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QWindow* m_window{};
    QSystemTrayIcon* m_trayIcon{};
    QMenu* m_menu{};
    QAction* m_startAction{};
    QAction* m_stopAction{};
    bool m_quitting{false};
};

#endif // TIKTOK_TELEGRAM_BOT_UI_TRAY_CONTROLLER_H
