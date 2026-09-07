#include "app/CacheManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QSettings>
#include <QStorageInfo>
#include <QtConcurrentRun>

#include <algorithm>
#include <utility>

namespace
{
bool linked(const QFileInfo& entry)
{
    return entry.isSymbolicLink() || entry.isJunction();
}
} // namespace

CachePolicy CachePolicy::fromSettings()
{
    QSettings settings;
    CachePolicy policy;
    policy.limitBytes =
        std::clamp(settings.value(QStringLiteral("cache/limitMiB"), 5120).toLongLong(), 256LL,
                   1048576LL) *
        1024 * 1024;
    policy.minimumFreeBytes =
        std::clamp(settings.value(QStringLiteral("cache/minFreeMiB"), 2048).toLongLong(), 128LL,
                   1048576LL) *
        1024 * 1024;
    policy.retentionDays =
        std::clamp(settings.value(QStringLiteral("cache/retentionDays"), 7).toInt(), 0, 365);
    return policy;
}

CacheManager::CacheManager(QString root, QObject* parent)
    : QObject(parent)
    , m_root(std::move(root))
    , m_policy(CachePolicy::fromSettings())
{
    connect(&m_watcher, &QFutureWatcher<CacheSnapshot>::finished, this,
            [this]
            {
                m_snapshot = m_watcher.result();
                emit snapshotChanged();
            });
}

void CacheManager::refresh(QList<qint64> disposableIds, const bool clean)
{
    if (m_watcher.isRunning())
        return;
    m_watcher.setFuture(QtConcurrent::run(&CacheManager::inspect, m_root, std::move(disposableIds),
                                          m_policy, clean, QDateTime::currentDateTimeUtc()));
}

void CacheManager::reloadPolicy()
{
    m_policy = CachePolicy::fromSettings();
    emit snapshotChanged();
}

bool CacheManager::hasCapacity(const qint64 reservationBytes) const
{
    return !m_watcher.isRunning() && m_snapshot.ready && reservationBytes >= 0 &&
           reservationBytes <= m_policy.limitBytes - m_snapshot.bytes &&
           reservationBytes <= m_snapshot.availableBytes - m_policy.minimumFreeBytes;
}

QString CacheManager::statusText() const
{
    if (!m_snapshot.ready)
        return m_snapshot.error.isEmpty() ? tr("Checking cache storage…") : m_snapshot.error;
    return tr("Cache %1 MiB / %2 MiB; free disk %3 MiB. Downloads resume when enough space is "
              "available.")
        .arg(m_snapshot.bytes / 1048576)
        .arg(m_policy.limitBytes / 1048576)
        .arg(m_snapshot.availableBytes / 1048576);
}
qint64 CacheManager::bytesUsed() const
{
    return m_snapshot.bytes;
}
qint64 CacheManager::bytesAvailable() const
{
    return m_snapshot.availableBytes;
}
bool CacheManager::isReady() const
{
    return m_snapshot.ready;
}
bool CacheManager::isBusy() const
{
    return m_watcher.isRunning();
}
CacheSnapshot CacheManager::snapshot() const
{
    return m_snapshot;
}

CacheSnapshot CacheManager::inspect(const QString& root, const QList<qint64>& disposableIds,
                                    const CachePolicy& policy, const bool clean,
                                    const QDateTime& nowUtc)
{
    CacheSnapshot result;
    QFileInfo rootInfo(root);
    if (!rootInfo.isAbsolute() || rootInfo.fileName() != QStringLiteral("video-cache") ||
        linked(rootInfo) || !QDir().mkpath(root))
    {
        result.error = QStringLiteral("Cache directory is unavailable or is a filesystem link.");
        return result;
    }
    rootInfo.refresh();
    const QString canonicalRoot = rootInfo.canonicalFilePath();
    if (canonicalRoot.isEmpty())
        return result;
    QSet<QString> disposable;
    for (qint64 id : disposableIds)
        if (id > 0)
            disposable.insert(QString::number(id));
    const QDateTime cutoff = nowUtc.addDays(-policy.retentionDays);
    QList<QString> pending{canonicalRoot};
    while (!pending.isEmpty())
    {
        const QString directory = pending.takeLast();
        const QFileInfoList files = QDir(directory).entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        const QString relative = QDir(canonicalRoot).relativeFilePath(directory);
        const bool eligible = clean && disposable.contains(relative) &&
                              QFileInfo(directory).canonicalFilePath() == directory;
        for (const QFileInfo& entry : files)
        {
            if (linked(entry))
                continue;
            if (entry.isDir())
            {
                pending.append(entry.absoluteFilePath());
                continue;
            }
            if (!entry.isFile())
                continue;
            bool removed = false;
            if (eligible && entry.lastModified().toUTC() <= cutoff &&
                entry.canonicalFilePath() == entry.absoluteFilePath())
            {
                removed = QFile::remove(entry.absoluteFilePath());
                if (removed)
                    ++result.removedFiles;
                else
                    result.error =
                        QStringLiteral("Some completed cache files could not be removed.");
            }
            if (!removed)
                result.bytes += entry.size();
        }
        // Never remove recursively; retained or unfamiliar content prevents removal.
        if (eligible)
            QDir().rmdir(directory);
    }
    const QStorageInfo storage(canonicalRoot);
    result.availableBytes = storage.bytesAvailable();
    result.ready = storage.isValid() && storage.isReady() && result.availableBytes >= 0;
    return result;
}
