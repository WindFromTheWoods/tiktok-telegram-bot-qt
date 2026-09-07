/******************************************************************************
 * @file    UpdateManager.h
 * @brief   Declares automatic application and yt-dlp update checks.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#ifndef TIKTOK_TELEGRAM_BOT_APP_UPDATE_MANAGER_H
#define TIKTOK_TELEGRAM_BOT_APP_UPDATE_MANAGER_H

#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

/**
 * @brief Checks for application and yt-dlp updates without blocking the UI.
 *
 * Application installation is delegated to the Qt Installer Framework maintenance
 * tool. The manager does not download or execute arbitrary update payloads itself.
 */
class UpdateManager final : public QObject
{
    Q_OBJECT
    /** @brief User-facing application update status. */
    Q_PROPERTY(QString applicationStatus READ applicationStatus NOTIFY statusChanged)
    /** @brief User-facing yt-dlp update status. */
    Q_PROPERTY(QString ytDlpStatus READ ytDlpStatus NOTIFY statusChanged)
    /** @brief Whether any update operation is active. */
    Q_PROPERTY(bool checking READ checking NOTIFY statusChanged)
    /** @brief Whether an app update is available. */
    Q_PROPERTY(bool applicationUpdateAvailable READ applicationUpdateAvailable NOTIFY statusChanged)
    /** @brief Whether installed updates are supported. */
    Q_PROPERTY(bool maintenanceToolAvailable READ maintenanceToolAvailable CONSTANT)

public:
    /**
     * @brief Constructs the update manager.
     *
     * @param[in] parent Parent object that owns this instance; may be nullptr.
     */
    explicit UpdateManager(QObject* parent = nullptr);

    /** @return User-facing application update status. */
    [[nodiscard]] QString applicationStatus() const;
    /** @return User-facing yt-dlp update status. */
    [[nodiscard]] QString ytDlpStatus() const;
    /** @return `true` while one or more update operations are active. */
    [[nodiscard]] bool checking() const noexcept;
    /** @return `true` when the maintenance tool reports an application update. */
    [[nodiscard]] bool applicationUpdateAvailable() const noexcept;
    /** @return `true` when an installed maintenance tool can service the application. */
    [[nodiscard]] bool maintenanceToolAvailable() const;

    /**
     * @brief Replaces the executable used for yt-dlp checks and updates.
     * @param[in] executable Executable name or absolute path.
     */
    void setYtDlpExecutable(const QString& executable);
    /** @brief Starts the daily update timer and schedules an initial check. */
    void startAutomaticChecks();

    /** @brief Checks both the application and yt-dlp versions asynchronously. */
    Q_INVOKABLE void checkForUpdates();
    /** @brief Runs yt-dlp's trusted self-update command. */
    Q_INVOKABLE void updateYtDlp();
    /** @brief Starts the installed maintenance tool for an available update. */
    Q_INVOKABLE void installApplicationUpdate();

signals:
    /** @brief Indicates that status properties changed. */
    void statusChanged();
    /**
     * @brief Delivers a sanitized update activity entry.
     * @param[in] level Log severity name.
     * @param[in] message Human-readable event text.
     */
    void activity(const QString& level, const QString& message);
    /** @brief Requests a clean application shutdown before maintenance. */
    void applicationRestartRequested();

private:
    [[nodiscard]] QString maintenanceToolPath() const;
    void checkApplicationUpdate();
    void checkYtDlpVersion();
    void finishOperation();

    QProcess m_applicationProcess;
    QProcess m_ytDlpProcess;
    QTimer m_automaticTimer;
    QString m_ytDlpExecutable{QStringLiteral("yt-dlp")};
    QString m_applicationStatus{QStringLiteral("Not checked")};
    QString m_ytDlpStatus{QStringLiteral("Not checked")};
    bool m_applicationUpdateAvailable{false};
    int m_pendingOperations{};
};

#endif // TIKTOK_TELEGRAM_BOT_APP_UPDATE_MANAGER_H
