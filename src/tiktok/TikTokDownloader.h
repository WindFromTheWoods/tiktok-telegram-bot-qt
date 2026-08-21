#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QUrl>

#include <memory>

class QTemporaryDir;

class TikTokDownloader final : public QObject
{
    Q_OBJECT

public:
    explicit TikTokDownloader(QString ytDlpExecutable, QObject *parent = nullptr);
    ~TikTokDownloader() override;

    void download(const QUrl &url);
    void cleanup();
    void cancel();

    [[nodiscard]] bool isBusy() const noexcept;

signals:
    void downloadStarted();
    void downloadFinished(const QString &filePath);
    void downloadFailed(const QString &error);

private slots:
    void collectStandardOutput();
    void collectStandardError();
    void onProcessError(QProcess::ProcessError error);
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    void fail(const QString &error);
    [[nodiscard]] QString downloadedFilePath() const;
    [[nodiscard]] QString sanitizedDiagnostic() const;

    QString m_ytDlpExecutable;
    std::unique_ptr<QTemporaryDir> m_temporaryDir;
    QProcess m_process;
    QByteArray m_standardOutput;
    QByteArray m_standardError;
    bool m_busy{false};
    bool m_completionReported{false};
};
