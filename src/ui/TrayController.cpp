/******************************************************************************
 * @file    TrayController.cpp
 * @brief   Implements system-tray controls and close-to-tray behavior.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#include "ui/TrayController.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QMenu>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QWindow>

TrayController::TrayController(QWindow* window, QObject* parent)
    : QObject(parent)
    , m_window(window)
    , m_trayIcon(new QSystemTrayIcon(this))
    , m_menu(new QMenu)
{
    if (m_window != nullptr)
    {
        m_window->installEventFilter(this);
    }
    m_trayIcon->setIcon(QApplication::style()->standardIcon(QStyle::SP_MediaPlay));
    m_trayIcon->setToolTip(QStringLiteral("TikTok Telegram Bot"));
    m_trayIcon->setContextMenu(m_menu);

    QAction* showAction = m_menu->addAction(QStringLiteral("Open application"));
    m_menu->addSeparator();
    m_startAction = m_menu->addAction(QStringLiteral("Start bot"));
    m_stopAction = m_menu->addAction(QStringLiteral("Stop bot"));
    m_menu->addSeparator();
    QAction* quitAction = m_menu->addAction(QStringLiteral("Exit"));

    connect(showAction, &QAction::triggered, this, &TrayController::showWindow);
    connect(m_startAction, &QAction::triggered, this, &TrayController::startRequested);
    connect(m_stopAction, &QAction::triggered, this, &TrayController::stopRequested);
    connect(quitAction, &QAction::triggered, this,
            [this]
            {
                m_quitting = true;
                emit quitRequested();
            });
    connect(m_trayIcon, &QSystemTrayIcon::activated, this,
            [this](const QSystemTrayIcon::ActivationReason reason)
            {
                if (reason == QSystemTrayIcon::DoubleClick || reason == QSystemTrayIcon::Trigger)
                {
                    showWindow();
                }
            });

    m_trayIcon->show();
    setBotRunning(false);
}

TrayController::~TrayController()
{
    if (m_window != nullptr)
    {
        m_window->removeEventFilter(this);
    }
    delete m_menu;
}

void TrayController::setBotRunning(const bool running)
{
    m_startAction->setEnabled(!running);
    m_stopAction->setEnabled(running);
    m_trayIcon->setToolTip(running ? QStringLiteral("TikTok Telegram Bot — running")
                                   : QStringLiteral("TikTok Telegram Bot — stopped"));
}

void TrayController::showNotification(const QString& title, const QString& message)
{
    if (QSystemTrayIcon::supportsMessages())
    {
        m_trayIcon->showMessage(title, message, QSystemTrayIcon::Information, 5000);
    }
}

void TrayController::showWindow()
{
    if (m_window != nullptr)
    {
        m_window->show();
        m_window->raise();
        m_window->requestActivate();
    }
}

void TrayController::hideWindow()
{
    if (m_window != nullptr)
    {
        m_window->hide();
    }
}

bool TrayController::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_window && event->type() == QEvent::Close && !m_quitting &&
        QSystemTrayIcon::isSystemTrayAvailable())
    {
        static_cast<QCloseEvent*>(event)->ignore();
        hideWindow();
        showNotification(QStringLiteral("TikTok Telegram Bot"),
                         QStringLiteral("The bot continues running in the system tray."));
        return true;
    }
    return QObject::eventFilter(watched, event);
}
