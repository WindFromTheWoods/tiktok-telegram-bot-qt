/******************************************************************************
 * @file    TikTokDownloader.cpp
 * @brief   Implements shell-free TikTok media downloads through yt-dlp.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#include "tiktok/TikTokDownloader.h"

#include "Logging.h"
#include "tiktok/TikTokUrlValidator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <utility>

namespace
{

constexpr int DiagnosticCharacterLimit = 1500;

bool isInsideDirectory(const QString& filePath, const QString& directoryPath)
{
    const QString canonicalFile = QFileInfo(filePath).canonicalFilePath();
    const QString canonicalDirectory = QFileInfo(directoryPath).canonicalFilePath();
    if (canonicalFile.isEmpty() || canonicalDirectory.isEmpty())
    {
        return false;
    }

    const QString relative = QDir(canonicalDirectory).relativeFilePath(canonicalFile);
    return relative != QStringLiteral("..") && !relative.startsWith(QStringLiteral("../")) &&
           !QDir::isAbsolutePath(relative);
}

} // namespace

TikTokDownloader::TikTokDownloader(QString ytDlpExecutable, QObject* parent)
    : QObject(parent)
    , m_ytDlpExecutable(std::move(ytDlpExecutable))
{
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    connect(&m_process, &QProcess::readyReadStandardOutput, this,
            &TikTokDownloader::collectStandardOutput);
    connect(&m_process, &QProcess::readyReadStandardError, this,
            &TikTokDownloader::collectStandardError);
    connect(&m_process, &QProcess::errorOccurred, this, &TikTokDownloader::onProcessError);
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            &TikTokDownloader::onProcessFinished);
    m_watchdog.setInterval(1000);
    connect(&m_watchdog, &QTimer::timeout, this,
            [this]
            {
                qint64 bytes = 0;
                for (const QFileInfo& file :
                     QDir(m_outputDirectory).entryInfoList(QDir::Files | QDir::NoSymLinks))
                    bytes += file.size();
                const QStorageInfo storage(m_outputDirectory);
                if (m_elapsed.elapsed() > 20 * 60 * 1000)
                    m_limitError = QStringLiteral("Download exceeded the 20 minute time limit.");
                else if (bytes > m_maximumBytes * 2 + 32LL * 1024 * 1024)
                    m_limitError = QStringLiteral("Download exceeded its working-space limit.");
                else if (storage.isValid() && storage.isReady() &&
                         storage.bytesAvailable() < 128LL * 1024 * 1024)
                    m_limitError =
                        QStringLiteral("Download stopped because disk space is critically low.");
                if (!m_limitError.isEmpty())
                {
                    m_watchdog.stop();
                    m_process.kill();
                }
            });
}

TikTokDownloader::~TikTokDownloader()
{
    cancel();
}

void TikTokDownloader::download(const QUrl& url, const QString& outputDirectory,
                                const qint64 maximumBytes)
{
    if (m_busy || m_process.state() != QProcess::NotRunning)
    {
        emit downloadFailed(QStringLiteral("A TikTok download is already in progress."));
        return;
    }
    if (!TikTokUrlValidator::isValid(url))
    {
        emit downloadFailed(QStringLiteral("The supplied TikTok URL is invalid."));
        return;
    }

    cleanup();
    if (outputDirectory.trimmed().isEmpty())
    {
        m_temporaryDir = std::make_unique<QTemporaryDir>(
            QDir::tempPath() + QStringLiteral("/TikTokTelegramBot-XXXXXX"));
        if (!m_temporaryDir->isValid())
        {
            m_temporaryDir.reset();
            emit downloadFailed(QStringLiteral("Could not create a temporary download directory."));
            return;
        }
        m_outputDirectory = m_temporaryDir->path();
    }
    else
    {
        m_outputDirectory = QDir::cleanPath(outputDirectory);
        if (!QDir().mkpath(m_outputDirectory))
        {
            emit downloadFailed(QStringLiteral("Could not create the video cache directory."));
            return;
        }
    }

    m_standardOutput.clear();
    m_standardError.clear();
    m_completionReported = false;
    m_busy = true;
    ++m_runId;
    m_limitError.clear();
    m_maximumBytes = std::clamp(maximumBytes, 1LL, 2LL * 1024 * 1024 * 1024);

    const QString outputTemplate =
        QDir(m_outputDirectory).filePath(QStringLiteral("%(id)s.%(ext)s"));
    const QStringList arguments{
        QStringLiteral("--ignore-config"),
        QStringLiteral("--max-filesize"),
        QString::number(m_maximumBytes),
        QStringLiteral("--socket-timeout"),
        QStringLiteral("30"),
        QStringLiteral("--retries"),
        QStringLiteral("3"),
        QStringLiteral("--fragment-retries"),
        QStringLiteral("3"),
        QStringLiteral("--no-playlist"),
        QStringLiteral("--restrict-filenames"),
        QStringLiteral("--newline"),
        QStringLiteral("--progress-template"),
        QStringLiteral(
            "download:%(progress._percent_str)s|%(progress._speed_str)s|%(progress._eta_str)s"),
        QStringLiteral("--write-info-json"),
        QStringLiteral("--write-thumbnail"),
        QStringLiteral("--format"),
        QStringLiteral("bv*[ext=mp4]+ba[ext=m4a]/b[ext=mp4]/bv*+ba/b"),
        QStringLiteral("--merge-output-format"),
        QStringLiteral("mp4"),
        QStringLiteral("--print"),
        QStringLiteral("after_move:filepath"),
        QStringLiteral("-o"),
        outputTemplate,
        url.toString(QUrl::FullyEncoded),
    };

    qCInfo(logTikTok) << "Starting yt-dlp";
    emit downloadStarted();
    m_process.start(m_ytDlpExecutable, arguments, QIODevice::ReadOnly);
    m_elapsed.start();
    m_watchdog.start();
}

void TikTokDownloader::cleanup()
{
    if (m_process.state() != QProcess::NotRunning)
    {
        return;
    }
    m_temporaryDir.reset();
    m_outputDirectory.clear();
    m_standardOutput.clear();
    m_standardError.clear();
}

void TikTokDownloader::cancel()
{
    m_watchdog.stop();
    if (m_process.state() == QProcess::NotRunning)
    {
        m_busy = false;
        return;
    }

    m_busy = false;
    m_completionReported = true;
    m_process.terminate();
    QPointer<QProcess> process(&m_process);
    const quint64 runId = m_runId;
    QTimer::singleShot(std::chrono::seconds(2), this,
                       [this, process, runId]
                       {
                           if (runId == m_runId && process &&
                               process->state() != QProcess::NotRunning)
                           {
                               process->kill();
                           }
                       });
}

bool TikTokDownloader::isBusy() const noexcept
{
    return m_busy || m_process.state() != QProcess::NotRunning;
}

void TikTokDownloader::collectStandardOutput()
{
    m_standardOutput.append(m_process.readAllStandardOutput());
    m_standardOutput = m_standardOutput.right(64 * 1024);
    const QString output = QString::fromUtf8(m_standardOutput);
    static const QRegularExpression progressPattern(
        QStringLiteral(R"(download:\s*([0-9]+(?:\.[0-9]+)?)%\|([^\r\n]*))"));
    QRegularExpressionMatchIterator matches = progressPattern.globalMatch(output);
    QRegularExpressionMatch lastMatch;
    while (matches.hasNext())
    {
        lastMatch = matches.next();
    }
    if (lastMatch.hasMatch())
    {
        emit downloadProgress(lastMatch.captured(1).toDouble(), lastMatch.captured(2).trimmed());
    }
}

void TikTokDownloader::collectStandardError()
{
    m_standardError.append(m_process.readAllStandardError());
    m_standardError = m_standardError.right(64 * 1024);
}

void TikTokDownloader::onProcessError(const QProcess::ProcessError error)
{
    collectStandardOutput();
    collectStandardError();
    if (m_completionReported)
    {
        return;
    }

    if (error == QProcess::FailedToStart)
    {
        fail(QStringLiteral(
            "yt-dlp could not be started. Install it or set YTDLP_PATH to the executable."));
    }
    else if (error == QProcess::Crashed)
    {
        // The finished handler runs after the child has stopped, so the next job can start.
        return;
    }
}

void TikTokDownloader::onProcessFinished(const int exitCode, const QProcess::ExitStatus exitStatus)
{
    collectStandardOutput();
    collectStandardError();
    m_busy = false;
    m_watchdog.stop();

    if (m_completionReported)
    {
        return;
    }
    if (!m_limitError.isEmpty())
    {
        fail(m_limitError);
        return;
    }
    if (exitStatus != QProcess::NormalExit)
    {
        fail(QStringLiteral("yt-dlp terminated unexpectedly."));
        return;
    }
    if (exitCode != 0)
    {
        const QString diagnostic = sanitizedDiagnostic();
        fail(diagnostic.isEmpty()
                 ? QStringLiteral("yt-dlp could not download this video (exit code %1).")
                       .arg(exitCode)
                 : diagnostic);
        return;
    }

    const QString filePath = downloadedFilePath();
    const QFileInfo fileInfo(filePath);
    if (filePath.isEmpty() || !fileInfo.isFile() || fileInfo.size() <= 0)
    {
        fail(QStringLiteral("yt-dlp finished but did not produce a usable video file."));
        return;
    }
    if (fileInfo.size() > m_maximumBytes)
    {
        fail(QStringLiteral("Downloaded video exceeds the configured upload limit."));
        return;
    }

    m_completionReported = true;
    qCInfo(logTikTok) << "yt-dlp completed";
    emit metadataAvailable(extractedMetadata());
    emit downloadFinished(filePath);
}

void TikTokDownloader::fail(const QString& error)
{
    if (m_completionReported)
    {
        return;
    }
    m_completionReported = true;
    m_busy = false;
    m_watchdog.stop();
    qCWarning(logTikTok) << "yt-dlp failed:" << error;
    emit downloadFailed(error);
}

QString TikTokDownloader::downloadedFilePath() const
{
    if (m_outputDirectory.isEmpty())
    {
        return {};
    }

    const QString output = QString::fromUtf8(m_standardOutput);
    const QStringList lines =
        output.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
    for (auto iterator = lines.crbegin(); iterator != lines.crend(); ++iterator)
    {
        QString candidate = iterator->trimmed();
        if (candidate.size() >= 2 && candidate.startsWith(QLatin1Char('"')) &&
            candidate.endsWith(QLatin1Char('"')))
        {
            candidate = candidate.mid(1, candidate.size() - 2);
        }
        if (QDir::isRelativePath(candidate))
        {
            candidate = QDir(m_outputDirectory).absoluteFilePath(candidate);
        }
        if (QFileInfo(candidate).isFile() && isInsideDirectory(candidate, m_outputDirectory))
        {
            return QFileInfo(candidate).canonicalFilePath();
        }
    }
    return {};
}

TikTokMetadata TikTokDownloader::extractedMetadata() const
{
    TikTokMetadata metadata;
    if (m_outputDirectory.isEmpty())
    {
        return metadata;
    }

    const QFileInfoList jsonFiles =
        QDir(m_outputDirectory)
            .entryInfoList({QStringLiteral("*.info.json")}, QDir::Files, QDir::Time);
    if (!jsonFiles.isEmpty())
    {
        QFile metadataFile(jsonFiles.first().absoluteFilePath());
        if (metadataFile.size() <= 4 * 1024 * 1024 &&
            isInsideDirectory(metadataFile.fileName(), m_outputDirectory) &&
            metadataFile.open(QIODevice::ReadOnly))
        {
            const QJsonObject object = QJsonDocument::fromJson(metadataFile.readAll()).object();
            metadata.videoId = object.value(QStringLiteral("id")).toString();
            metadata.title = object.value(QStringLiteral("title")).toString();
            metadata.author = object.value(QStringLiteral("uploader")).toString();
            if (metadata.author.isEmpty())
            {
                metadata.author = object.value(QStringLiteral("channel")).toString();
            }
            metadata.durationSeconds = object.value(QStringLiteral("duration")).toInt();
        }
    }

    const QFileInfoList thumbnails =
        QDir(m_outputDirectory)
            .entryInfoList({QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"),
                            QStringLiteral("*.png"), QStringLiteral("*.webp")},
                           QDir::Files, QDir::Time);
    if (!thumbnails.isEmpty())
    {
        metadata.thumbnailPath = thumbnails.first().canonicalFilePath();
    }
    return metadata;
}

QString TikTokDownloader::sanitizedDiagnostic() const
{
    QString diagnostic = QString::fromUtf8(m_standardError).trimmed();
    if (diagnostic.isEmpty())
    {
        return {};
    }
    if (!m_outputDirectory.isEmpty())
    {
        diagnostic.replace(m_outputDirectory, QStringLiteral("<download directory>"),
                           Qt::CaseInsensitive);
    }
    diagnostic.remove(QRegularExpression(QStringLiteral("[\\x00-\\x08\\x0B\\x0C\\x0E-\\x1F]")));
    if (diagnostic.size() > DiagnosticCharacterLimit)
    {
        diagnostic = QStringLiteral("...") + diagnostic.right(DiagnosticCharacterLimit);
    }
    return diagnostic;
}
