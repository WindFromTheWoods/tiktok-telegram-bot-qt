#include "app/CacheManager.h"
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class TestCacheManager : public QObject
{
    Q_OBJECT
private slots:
    void cleanupOnlyTerminalFiles()
    {
        QTemporaryDir dir;
        const QString root = dir.filePath(QStringLiteral("video-cache"));
        for (const QString& id :
             {QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("unknown")})
        {
            QVERIFY(QDir().mkpath(root + '/' + id));
            QFile file(root + '/' + id + QStringLiteral("/video.mp4"));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("test"), 4);
        }
        CachePolicy policy;
        policy.retentionDays = 0;
        auto scan = CacheManager::inspect(root, {1}, policy, true,
                                          QDateTime::currentDateTimeUtc().addSecs(2));
        QVERIFY(scan.ready);
        QCOMPARE(scan.removedFiles, 1);
        QCOMPARE(scan.bytes, 8);
        QVERIFY(QFile::exists(root + QStringLiteral("/2/video.mp4")));
        QVERIFY(QFile::exists(root + QStringLiteral("/unknown/video.mp4")));
    }
    void retentionAndRootGuard()
    {
        QTemporaryDir dir;
        auto invalid =
            CacheManager::inspect(dir.path(), {}, {}, true, QDateTime::currentDateTimeUtc());
        QVERIFY(!invalid.ready);
        const QString root = dir.filePath(QStringLiteral("video-cache"));
        QVERIFY(QDir().mkpath(root + QStringLiteral("/1")));
        QFile file(root + QStringLiteral("/1/video.mp4"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("keep"), 4);
        file.close();
        auto scan = CacheManager::inspect(root, {1}, {}, true, QDateTime::currentDateTimeUtc());
        QCOMPARE(scan.removedFiles, 0);
        QCOMPARE(scan.bytes, 4);
    }
    void backgroundScanAndCapacity()
    {
        QTemporaryDir dir;
        CacheManager manager(dir.filePath(QStringLiteral("video-cache")));
        QVERIFY(!manager.hasCapacity(1));
        QSignalSpy spy(&manager, &CacheManager::snapshotChanged);
        manager.refresh();
        QTRY_VERIFY(!spy.isEmpty());
        QVERIFY(manager.isReady());
        QVERIFY(!manager.hasCapacity(-1));
        QVERIFY(!manager.hasCapacity(2000000000000LL));
    }
};
QTEST_GUILESS_MAIN(TestCacheManager)
#include "TestCacheManager.moc"
