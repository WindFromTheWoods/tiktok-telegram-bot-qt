#ifndef TIKTOK_BOT_CACHE_MANAGER_H
#define TIKTOK_BOT_CACHE_MANAGER_H

#include <QDateTime>
#include <QFutureWatcher>
#include <QObject>
#include <QString>

/** @brief User-configurable storage budget, shared by desktop and queue workers. */
struct CachePolicy
{
    qint64 limitBytes{5120LL * 1024 * 1024};       /**< Total cache budget. */
    qint64 minimumFreeBytes{2048LL * 1024 * 1024}; /**< Space reserved for other applications. */
    int retentionDays{7};                          /**< Completed/cancelled cache retention. */
    /** @return Validated persistent policy or conservative defaults. */
    static CachePolicy fromSettings();
};

/** @brief Immutable result of a background cache scan. */
struct CacheSnapshot
{
    qint64 bytes{};          /**< Cache size, without following directory links. */
    qint64 availableBytes{}; /**< Available storage on the cache volume. */
    int removedFiles{};      /**< Files removed from explicitly disposable video folders. */
    bool ready{false};       /**< Whether the scan could establish the storage state. */
    QString error;           /**< Scan or deletion diagnostic. */
};

/** @brief Measures and cleans owned media files without blocking the GUI thread. */
class CacheManager final : public QObject
{
    Q_OBJECT
public:
    /** @brief Creates a scanner for one dedicated video-cache directory. */
    explicit CacheManager(QString root, QObject* parent = nullptr);
    /** @brief Schedules one scan; clean only touches explicitly disposable IDs. */
    void refresh(QList<qint64> disposableIds = {}, bool clean = false);
    /** @brief Reloads user policy after settings change. */
    void reloadPolicy();
    /** @return Whether storage can accommodate a bounded new download. */
    [[nodiscard]] bool hasCapacity(qint64 reservationBytes) const;
    /** @return A reason why downloads may be paused. */
    [[nodiscard]] QString statusText() const;
    /** @return Most recently scanned cache size. */
    [[nodiscard]] qint64 bytesUsed() const;
    /** @return Most recently scanned free space. */
    [[nodiscard]] qint64 bytesAvailable() const;
    /** @return Whether a usable scan has completed. */
    [[nodiscard]] bool isReady() const;
    /** @return Whether a scan is running. */
    [[nodiscard]] bool isBusy() const;
    /** @return Last immutable scan result. */
    [[nodiscard]] CacheSnapshot snapshot() const;
    /** @brief Standalone worker; never follows symlinks/junctions or unknown subfolders. */
    [[nodiscard]] static CacheSnapshot inspect(const QString& root,
                                               const QList<qint64>& disposableIds,
                                               const CachePolicy& policy, bool clean,
                                               const QDateTime& nowUtc);
signals:
    /** @brief A scan finished, or settings changed; queue capacity may have changed. */
    void snapshotChanged();

private:
    QString m_root;
    CachePolicy m_policy;
    CacheSnapshot m_snapshot;
    QFutureWatcher<CacheSnapshot> m_watcher;
};

#endif
