#pragma once

#include "app/BotController.h"

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>

#include <memory>

class AppConfig;

class SettingsController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString botToken READ botToken WRITE setBotToken NOTIFY settingsChanged)
    Q_PROPERTY(QString channelId READ channelId WRITE setChannelId NOTIFY settingsChanged)
    Q_PROPERTY(QString adminUserId READ adminUserId WRITE setAdminUserId NOTIFY settingsChanged)
    Q_PROPERTY(QString ytDlpExecutable READ ytDlpExecutable WRITE setYtDlpExecutable
                   NOTIFY settingsChanged)
    Q_PROPERTY(int publicationIntervalMinutes READ publicationIntervalMinutes
                   WRITE setPublicationIntervalMinutes NOTIFY settingsChanged)
    Q_PROPERTY(bool botRunning READ botRunning NOTIFY botRunningChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusChanged)
    Q_PROPERTY(bool statusIsError READ statusIsError NOTIFY statusChanged)
    Q_PROPERTY(bool secureSecretStorage READ secureSecretStorage CONSTANT)
    Q_PROPERTY(QString secretStorageDescription READ secretStorageDescription CONSTANT)
    Q_PROPERTY(QVariantList scheduledVideos READ scheduledVideos NOTIFY videoCatalogChanged)
    Q_PROPERTY(QVariantList sentVideos READ sentVideos NOTIFY videoCatalogChanged)

public:
    explicit SettingsController(QObject *parent = nullptr);
    ~SettingsController() override;

    [[nodiscard]] QString botToken() const;
    [[nodiscard]] QString channelId() const;
    [[nodiscard]] QString adminUserId() const;
    [[nodiscard]] QString ytDlpExecutable() const;
    [[nodiscard]] int publicationIntervalMinutes() const noexcept;
    [[nodiscard]] bool botRunning() const noexcept;
    [[nodiscard]] QString statusMessage() const;
    [[nodiscard]] bool statusIsError() const noexcept;
    [[nodiscard]] bool secureSecretStorage() const noexcept;
    [[nodiscard]] QString secretStorageDescription() const;
    [[nodiscard]] QVariantList scheduledVideos() const;
    [[nodiscard]] QVariantList sentVideos() const;

    void setBotToken(const QString &value);
    void setChannelId(const QString &value);
    void setAdminUserId(const QString &value);
    void setYtDlpExecutable(const QString &value);
    void setPublicationIntervalMinutes(int value);

    Q_INVOKABLE void saveAndStart();
    Q_INVOKABLE void stopBot();
    Q_INVOKABLE void startIfConfigured();
    Q_INVOKABLE void setYtDlpFromUrl(const QUrl &url);

signals:
    void settingsChanged();
    void botRunningChanged();
    void statusChanged();
    void videoCatalogChanged();

private:
    void loadSettings();
    void loadSentVideos();
    void saveSentVideos() const;
    void recordSentVideo(const QString &sourceUrl, const QDateTime &publishedAtUtc);
    [[nodiscard]] bool persistSettings(const AppConfig &config, QString *error);
    void startWithConfiguration(AppConfig config);
    void setBotRunning(bool running);
    void setStatus(QString message, bool isError);

    QString m_botToken;
    QString m_channelId;
    QString m_adminUserId;
    QString m_ytDlpExecutable{QStringLiteral("yt-dlp")};
    int m_publicationIntervalMinutes{120};
    std::unique_ptr<BotController> m_botController;
    QString m_statusMessage{QStringLiteral("Enter the settings and select Save & Start.")};
    QVariantList m_scheduledVideos;
    QVariantList m_sentVideos;
    bool m_botRunning{false};
    bool m_statusIsError{false};
};
