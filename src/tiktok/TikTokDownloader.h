/******************************************************************************
 * @file    TikTokDownloader.h
 * @brief   Declares asynchronous TikTok metadata extraction and media download.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#ifndef TIKTOK_TELEGRAM_BOT_TIKTOK_DOWNLOADER_H
#define TIKTOK_TELEGRAM_BOT_TIKTOK_DOWNLOADER_H

#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <memory>

class QTemporaryDir;

/** @brief Metadata extracted from one TikTok media item. */
struct TikTokMetadata
{
    QString videoId;       /**< TikTok's source video identifier. */
    QString title;         /**< Source title or description. */
    QString author;        /**< Source creator name. */
    QString thumbnailPath; /**< Local thumbnail path, when available. */
    int durationSeconds{}; /**< Media duration rounded to seconds. */
};

Q_DECLARE_METATYPE(TikTokMetadata)

/** @brief Runs one shell-free `yt-dlp` process and reports progress with signals. */
class TikTokDownloader final : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Constructs a downloader with Qt object ownership.
     *
     * @param[in] ytDlpExecutable Executable name or absolute path.
     * @param[in] parent Parent object that owns this instance; may be nullptr.
     */
    explicit TikTokDownloader(QString ytDlpExecutable, QObject* parent = nullptr);
    /** @brief Cancels any active process and releases temporary resources. */
    ~TikTokDownloader() override;

    /**
     * @brief Starts an asynchronous download.
     *
     * @param[in] url Validated TikTok URL.
     * @param[in] outputDirectory Persistent output directory; empty uses a temporary one.
     * @param[in] maximumBytes Maximum accepted media size, also used for a working-space limit.
     */
    void download(const QUrl& url, const QString& outputDirectory = {},
                  qint64 maximumBytes = 50LL * 1024 * 1024);
    /** @brief Removes resources retained by the completed or failed download. */
    void cleanup();
    /** @brief Terminates the active process and reports cancellation as failure. */
    void cancel();

    /** @return `true` while a `yt-dlp` process is active. */
    [[nodiscard]] bool isBusy() const noexcept;

signals:
    /** @brief Emitted after the child process starts. */
    void downloadStarted();
    /**
     * @brief Reports parsed progress.
     *
     * @param[in] percent Completion from 0 through 100.
     * @param[in] detail Sanitized human-readable progress detail.
     */
    void downloadProgress(double percent, const QString& detail);
    /**
     * @brief Delivers metadata parsed before or during the download.
     * @param[in] metadata Extracted media metadata.
     */
    void metadataAvailable(const TikTokMetadata& metadata);
    /**
     * @brief Reports successful download completion.
     * @param[in] filePath Absolute path of the completed media file.
     */
    void downloadFinished(const QString& filePath);
    /**
     * @brief Reports terminal download failure.
     * @param[in] error Sanitized failure description.
     */
    void downloadFailed(const QString& error);

private slots:
    void collectStandardOutput();
    void collectStandardError();
    void onProcessError(QProcess::ProcessError error);
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    void fail(const QString& error);
    [[nodiscard]] QString downloadedFilePath() const;
    [[nodiscard]] TikTokMetadata extractedMetadata() const;
    [[nodiscard]] QString sanitizedDiagnostic() const;

    QString m_ytDlpExecutable;
    QString m_outputDirectory;
    std::unique_ptr<QTemporaryDir> m_temporaryDir;
    QProcess m_process;
    QByteArray m_standardOutput;
    QByteArray m_standardError;
    QTimer m_watchdog;
    QElapsedTimer m_elapsed;
    QString m_limitError;
    qint64 m_maximumBytes{};
    quint64 m_runId{};
    bool m_busy{false};
    bool m_completionReported{false};
};

#endif // TIKTOK_TELEGRAM_BOT_TIKTOK_DOWNLOADER_H
