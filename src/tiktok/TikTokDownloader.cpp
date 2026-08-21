#include "tiktok/TikTokDownloader.h"

#include "Logging.h"
#include "tiktok/TikTokUrlValidator.h"

#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTimer>

#include <chrono>
#include <utility>

namespace {

constexpr int DiagnosticCharacterLimit = 1500;

bool isInsideDirectory(const QString &filePath, const QString &directoryPath)
{
    const QString canonicalFile = QFileInfo(filePath).canonicalFilePath();
    const QString canonicalDirectory = QFileInfo(directoryPath).canonicalFilePath();
    if (canonicalFile.isEmpty() || canonicalDirectory.isEmpty()) {
        return false;
    }

    const QString relative = QDir(canonicalDirectory).relativeFilePath(canonicalFile);
    return relative != QStringLiteral("..")
        && !relative.startsWith(QStringLiteral("../"))
        && !QDir::isAbsolutePath(relative);
}

} // namespace

TikTokDownloader::TikTokDownloader(QString ytDlpExecutable, QObject *parent)
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
}

TikTokDownloader::~TikTokDownloader()
{
    cancel();
}

void TikTokDownloader::download(const QUrl &url)
{
    if (m_busy) {
        emit downloadFailed(QStringLiteral("A TikTok download is already in progress."));
        return;
    }
    if (!TikTokUrlValidator::isValid(url)) {
        emit downloadFailed(QStringLiteral("The supplied TikTok URL is invalid."));
        return;
    }

    cleanup();
    m_temporaryDir = std::make_unique<QTemporaryDir>(
        QDir::tempPath() + QStringLiteral("/TikTokTelegramBot-XXXXXX"));
    if (!m_temporaryDir->isValid()) {
        m_temporaryDir.reset();
        emit downloadFailed(QStringLiteral("Could not create a temporary download directory."));
        return;
    }

    m_standardOutput.clear();
    m_standardError.clear();
    m_completionReported = false;
    m_busy = true;

    const QString outputTemplate =
        QDir(m_temporaryDir->path()).filePath(QStringLiteral("%(id)s.%(ext)s"));
    const QStringList arguments{
        QStringLiteral("--no-playlist"),
        QStringLiteral("--restrict-filenames"),
        QStringLiteral("--no-progress"),
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
}

void TikTokDownloader::cleanup()
{
    if (m_process.state() != QProcess::NotRunning) {
        return;
    }
    m_temporaryDir.reset();
    m_standardOutput.clear();
    m_standardError.clear();
}

void TikTokDownloader::cancel()
{
    if (m_process.state() == QProcess::NotRunning) {
        m_busy = false;
        return;
    }

    m_busy = false;
    m_completionReported = true;
    m_process.terminate();
    QPointer<QProcess> process(&m_process);
    QTimer::singleShot(std::chrono::seconds(2), this, [process] {
        if (process && process->state() != QProcess::NotRunning) {
            process->kill();
        }
    });
}

bool TikTokDownloader::isBusy() const noexcept
{
    return m_busy;
}

void TikTokDownloader::collectStandardOutput()
{
    m_standardOutput.append(m_process.readAllStandardOutput());
}

void TikTokDownloader::collectStandardError()
{
    m_standardError.append(m_process.readAllStandardError());
}

void TikTokDownloader::onProcessError(const QProcess::ProcessError error)
{
    collectStandardOutput();
    collectStandardError();
    if (m_completionReported) {
        return;
    }

    if (error == QProcess::FailedToStart) {
        fail(QStringLiteral(
            "yt-dlp could not be started. Install it or set YTDLP_PATH to the executable."));
    } else if (error == QProcess::Crashed) {
        fail(QStringLiteral("yt-dlp terminated unexpectedly."));
    }
}

void TikTokDownloader::onProcessFinished(const int exitCode,
                                         const QProcess::ExitStatus exitStatus)
{
    collectStandardOutput();
    collectStandardError();
    m_busy = false;

    if (m_completionReported) {
        return;
    }
    if (exitStatus != QProcess::NormalExit) {
        fail(QStringLiteral("yt-dlp terminated unexpectedly."));
        return;
    }
    if (exitCode != 0) {
        const QString diagnostic = sanitizedDiagnostic();
        fail(diagnostic.isEmpty()
                 ? QStringLiteral("yt-dlp could not download this video (exit code %1).")
                       .arg(exitCode)
                 : diagnostic);
        return;
    }

    const QString filePath = downloadedFilePath();
    const QFileInfo fileInfo(filePath);
    if (filePath.isEmpty() || !fileInfo.isFile() || fileInfo.size() <= 0) {
        fail(QStringLiteral("yt-dlp finished but did not produce a usable video file."));
        return;
    }

    m_completionReported = true;
    qCInfo(logTikTok) << "yt-dlp completed";
    emit downloadFinished(filePath);
}

void TikTokDownloader::fail(const QString &error)
{
    if (m_completionReported) {
        return;
    }
    m_completionReported = true;
    m_busy = false;
    qCWarning(logTikTok) << "yt-dlp failed:" << error;
    emit downloadFailed(error);
}

QString TikTokDownloader::downloadedFilePath() const
{
    if (!m_temporaryDir) {
        return {};
    }

    const QString output = QString::fromUtf8(m_standardOutput);
    const QStringList lines = output.split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                           Qt::SkipEmptyParts);
    for (auto iterator = lines.crbegin(); iterator != lines.crend(); ++iterator) {
        QString candidate = iterator->trimmed();
        if (candidate.size() >= 2 && candidate.startsWith(QLatin1Char('"'))
            && candidate.endsWith(QLatin1Char('"'))) {
            candidate = candidate.mid(1, candidate.size() - 2);
        }
        if (QDir::isRelativePath(candidate)) {
            candidate = QDir(m_temporaryDir->path()).absoluteFilePath(candidate);
        }
        if (QFileInfo(candidate).isFile()
            && isInsideDirectory(candidate, m_temporaryDir->path())) {
            return QFileInfo(candidate).canonicalFilePath();
        }
    }
    return {};
}

QString TikTokDownloader::sanitizedDiagnostic() const
{
    QString diagnostic = QString::fromUtf8(m_standardError).trimmed();
    if (diagnostic.isEmpty()) {
        return {};
    }
    if (m_temporaryDir) {
        diagnostic.replace(m_temporaryDir->path(), QStringLiteral("<temporary directory>"),
                           Qt::CaseInsensitive);
    }
    diagnostic.remove(QRegularExpression(QStringLiteral("[\\x00-\\x08\\x0B\\x0C\\x0E-\\x1F]")));
    if (diagnostic.size() > DiagnosticCharacterLimit) {
        diagnostic = QStringLiteral("...") + diagnostic.right(DiagnosticCharacterLimit);
    }
    return diagnostic;
}
