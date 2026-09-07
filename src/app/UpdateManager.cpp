/******************************************************************************
 * @file    UpdateManager.cpp
 * @brief   Implements automatic application and yt-dlp update checks.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#include "app/UpdateManager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>

#include <algorithm>
#include <chrono>

namespace
{

constexpr auto LastAutomaticCheckKey = "updates/lastAutomaticCheckUtc";

} // namespace

UpdateManager::UpdateManager(QObject* parent)
    : QObject(parent)
{
    m_automaticTimer.setInterval(std::chrono::hours(24));
    connect(&m_automaticTimer, &QTimer::timeout, this, &UpdateManager::checkForUpdates);

    connect(&m_applicationProcess, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](const int exitCode, QProcess::ExitStatus)
            {
                const QString output =
                    QString::fromUtf8(m_applicationProcess.readAllStandardOutput() +
                                      m_applicationProcess.readAllStandardError())
                        .trimmed();
                if (exitCode != 0)
                {
                    m_applicationUpdateAvailable = false;
                    m_applicationStatus =
                        output.isEmpty() ? QStringLiteral("Application update check failed")
                                         : QStringLiteral("Application update check failed: %1")
                                               .arg(output.left(1000));
                }
                else
                {
                    static const QRegularExpression updateElement(
                        QStringLiteral(R"(<update(?:\s|>))"),
                        QRegularExpression::CaseInsensitiveOption);
                    m_applicationUpdateAvailable = updateElement.match(output).hasMatch();
                    m_applicationStatus = m_applicationUpdateAvailable
                                              ? QStringLiteral("Application update is available")
                                              : QStringLiteral("Application is up to date");
                }
                finishOperation();
            });
    connect(&m_applicationProcess, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError)
            {
                m_applicationStatus = QStringLiteral("Application update check failed: %1")
                                          .arg(m_applicationProcess.errorString());
                finishOperation();
            });

    connect(&m_ytDlpProcess, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](const int exitCode, QProcess::ExitStatus)
            {
                const QString output = QString::fromUtf8(m_ytDlpProcess.readAllStandardOutput() +
                                                         m_ytDlpProcess.readAllStandardError())
                                           .trimmed();
                m_ytDlpStatus =
                    exitCode == 0
                        ? (output.isEmpty() ? QStringLiteral("yt-dlp is available") : output)
                        : QStringLiteral("yt-dlp check failed: %1").arg(output);
                finishOperation();
            });
    connect(
        &m_ytDlpProcess, &QProcess::errorOccurred, this,
        [this](QProcess::ProcessError)
        {
            m_ytDlpStatus =
                QStringLiteral("yt-dlp could not be started: %1").arg(m_ytDlpProcess.errorString());
            finishOperation();
        });
}

QString UpdateManager::applicationStatus() const
{
    return m_applicationStatus;
}

QString UpdateManager::ytDlpStatus() const
{
    return m_ytDlpStatus;
}

bool UpdateManager::checking() const noexcept
{
    return m_pendingOperations > 0;
}

bool UpdateManager::applicationUpdateAvailable() const noexcept
{
    return m_applicationUpdateAvailable;
}

bool UpdateManager::maintenanceToolAvailable() const
{
    return !maintenanceToolPath().isEmpty();
}

void UpdateManager::setYtDlpExecutable(const QString& executable)
{
    if (!executable.trimmed().isEmpty())
    {
        m_ytDlpExecutable = executable.trimmed();
    }
}

void UpdateManager::startAutomaticChecks()
{
    m_automaticTimer.start();
    QSettings settings;
    const QDateTime lastCheck = QDateTime::fromString(
        settings.value(QString::fromLatin1(LastAutomaticCheckKey)).toString(), Qt::ISODateWithMs);
    if (!lastCheck.isValid() || lastCheck.secsTo(QDateTime::currentDateTimeUtc()) >= 24 * 60 * 60)
    {
        QTimer::singleShot(std::chrono::seconds(15), this, &UpdateManager::checkForUpdates);
    }
}

void UpdateManager::checkForUpdates()
{
    if (checking())
    {
        return;
    }
    QSettings settings;
    settings.setValue(QString::fromLatin1(LastAutomaticCheckKey),
                      QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    checkApplicationUpdate();
    checkYtDlpVersion();
}

void UpdateManager::checkApplicationUpdate()
{
    const QString tool = maintenanceToolPath();
    if (tool.isEmpty())
    {
        m_applicationStatus =
            QStringLiteral("Updater is available after installation with Qt Installer Framework");
        emit statusChanged();
        return;
    }
    ++m_pendingOperations;
    m_applicationStatus = QStringLiteral("Checking application updates…");
    emit statusChanged();
    m_applicationProcess.start(tool, {QStringLiteral("check-updates")}, QIODevice::ReadOnly);
}

void UpdateManager::checkYtDlpVersion()
{
    ++m_pendingOperations;
    m_ytDlpStatus = QStringLiteral("Checking yt-dlp…");
    emit statusChanged();
    m_ytDlpProcess.start(m_ytDlpExecutable, {QStringLiteral("--version")}, QIODevice::ReadOnly);
}

void UpdateManager::updateYtDlp()
{
    if (m_ytDlpProcess.state() != QProcess::NotRunning)
    {
        return;
    }
    ++m_pendingOperations;
    m_ytDlpStatus = QStringLiteral("Updating yt-dlp…");
    emit statusChanged();
    emit activity(QStringLiteral("info"), QStringLiteral("yt-dlp update started."));
    m_ytDlpProcess.start(m_ytDlpExecutable, {QStringLiteral("-U")}, QIODevice::ReadOnly);
}

void UpdateManager::installApplicationUpdate()
{
    const QString tool = maintenanceToolPath();
    if (tool.isEmpty() || !m_applicationUpdateAvailable)
    {
        return;
    }
    if (QProcess::startDetached(tool, {QStringLiteral("update")}))
    {
        emit activity(QStringLiteral("info"), QStringLiteral("Application updater started."));
        emit applicationRestartRequested();
    }
}

QString UpdateManager::maintenanceToolPath() const
{
    const QString suffix =
#ifdef Q_OS_WIN
        QStringLiteral(".exe");
#else
        QString{};
#endif
    const QStringList candidates{
        QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("maintenancetool") + suffix),
        QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("../maintenancetool") + suffix),
    };
    for (const QString& candidate : candidates)
    {
        if (QFileInfo(candidate).isExecutable())
        {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }
    return {};
}

void UpdateManager::finishOperation()
{
    m_pendingOperations = std::max(0, m_pendingOperations - 1);
    emit statusChanged();
}
