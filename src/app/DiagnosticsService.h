#ifndef TIKTOK_BOT_DIAGNOSTICS_SERVICE_H
#define TIKTOK_BOT_DIAGNOSTICS_SERVICE_H
#include "storage/AppDatabase.h"
#include <QNetworkAccessManager>
#include <QObject>
#include <QVariantList>

/** @brief On-demand, asynchronous checks of local tools and Bot API channel permissions. */
class DiagnosticsService final : public QObject
{
    Q_OBJECT
public:
    /** @brief Creates an idle diagnostics service. */
    explicit DiagnosticsService(QObject* parent = nullptr);
    /** @return Latest rows with name/state/detail fields. */
    QVariantList results() const;
    /** @brief Starts checks without publishing any content or logging credentials. */
    void run(const QString& downloader, const QString& library, const QString& token,
             const QList<ChannelRecord>& channels, bool userPublisher);
signals:
    /** @brief One or more diagnostic rows changed. */
    void changed();

private:
    void put(const QString& name, const QString& state, const QString& detail);
    void checkTool(const QString& name, const QString& executable, const QStringList& arguments,
                   int generation);
    QNetworkAccessManager m_network;
    QVariantList m_results;
    int m_generation{};
};
#endif
