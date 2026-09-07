/******************************************************************************
 * @file    TestAppDatabase.cpp
 * @brief   Tests SQLite schema, queue transitions, and backup behavior.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#include "storage/AppDatabase.h"

#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

class TestAppDatabase final : public QObject
{
    Q_OBJECT

private slots:
    void createsSchemaAndManagesChannels();
    void persistsVideoLifecycleAndRejectsActiveDuplicate();
    void reordersAndRetriesQueueEntries();
    void persistsServerScheduledLifecycle();
    void migratesVersionOneSchema();
    void exportsAndImportsBackup();
};

void TestAppDatabase::createsSchemaAndManagesChannels()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    AppDatabase database(directory.filePath(QStringLiteral("app.sqlite")));
    QString error;
    QVERIFY2(database.open(&error), qPrintable(error));
    QVERIFY(QFileInfo::exists(database.databasePath()));

    const qint64 defaultId =
        database.ensureDefaultChannel(QStringLiteral("-1001234567890"), &error);
    QVERIFY2(defaultId > 0, qPrintable(error));
    QVERIFY(database.defaultChannel().has_value());
    QCOMPARE(database.defaultChannel()->id, defaultId);

    const qint64 secondId =
        database.addChannel(QStringLiteral("Secondary"), QStringLiteral("@second_channel"), &error);
    QVERIFY2(secondId > 0, qPrintable(error));
    QCOMPARE(database.channels().size(), 2);

    const qint64 slotId = database.addScheduleSlot(secondId, 5, QStringLiteral("18:30"), &error);
    QVERIFY2(slotId > 0, qPrintable(error));
    const QList<ScheduleSlotRecord> scheduleSlots = database.scheduleSlots(secondId);
    QCOMPARE(scheduleSlots.size(), 1);
    QCOMPARE(scheduleSlots.first().time, QStringLiteral("18:30"));

    QVERIFY2(database.setDefaultChannel(secondId, &error), qPrintable(error));
    QCOMPARE(database.defaultChannel()->id, secondId);
    QVERIFY2(database.disableChannel(defaultId, &error), qPrintable(error));
    QCOMPARE(database.channels().size(), 1);
}

void TestAppDatabase::persistsVideoLifecycleAndRejectsActiveDuplicate()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    AppDatabase database(directory.filePath(QStringLiteral("app.sqlite")));
    QString error;
    QVERIFY2(database.open(&error), qPrintable(error));
    const qint64 channelId =
        database.ensureDefaultChannel(QStringLiteral("-1001234567890"), &error);
    QVERIFY2(channelId > 0, qPrintable(error));

    const QString url = QStringLiteral("https://www.tiktok.com/@creator/video/1234567890");
    const QDateTime planned = QDateTime::currentDateTimeUtc().addSecs(3600);
    const qint64 videoId = database.addVideo(url, 100, 200, channelId, planned, &error);
    QVERIFY2(videoId > 0, qPrintable(error));
    QCOMPARE(database.addVideo(url, 100, 200, channelId, planned, &error), 0LL);
    QVERIFY(error.contains(QStringLiteral("already"), Qt::CaseInsensitive));

    QVERIFY2(database.markDownloading(videoId, &error), qPrintable(error));
    QVERIFY2(database.updateDownloadProgress(videoId, 45.5, &error), qPrintable(error));
    QVERIFY2(database.updateMetadata(videoId, QStringLiteral("Title"), QStringLiteral("creator"),
                                     91, directory.filePath(QStringLiteral("thumb.webp")), &error),
             qPrintable(error));
    QVERIFY2(database.markReady(videoId, directory.filePath(QStringLiteral("video.mp4")), 1'500'000,
                                &error),
             qPrintable(error));

    const std::optional<VideoRecord> ready = database.video(videoId);
    QVERIFY(ready.has_value());
    QCOMPARE(ready->status, QStringLiteral("ready"));
    QCOMPARE(ready->progress, 100.0);
    QCOMPARE(ready->title, QStringLiteral("Title"));
    QCOMPARE(ready->fileSize, 1'500'000LL);
    QVERIFY(!database.nextVideoForUpload(QDateTime::currentDateTimeUtc()).has_value());

    QVERIFY2(database.publishVideoNow(videoId, &error), qPrintable(error));
    QVERIFY(database.nextVideoForUpload(QDateTime::currentDateTimeUtc()).has_value());
    QVERIFY2(database.markUploading(videoId, &error), qPrintable(error));
    const QDateTime sentAt = QDateTime::currentDateTimeUtc();
    QVERIFY2(database.markSent(videoId, sentAt, &error), qPrintable(error));
    QCOMPARE(database.sentVideos().size(), 1);
    QCOMPARE(database.activeVideos().size(), 0);

    QCOMPARE(database.addVideo(url, 100, 200, channelId, planned, &error), 0LL);
    const qint64 repeatedId =
        database.addVideo(url, 100, 200, channelId, planned, &error, true, true);
    QVERIFY2(repeatedId > 0, qPrintable(error));
}

void TestAppDatabase::reordersAndRetriesQueueEntries()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    AppDatabase database(directory.filePath(QStringLiteral("app.sqlite")));
    QString error;
    QVERIFY2(database.open(&error), qPrintable(error));
    const qint64 channelId =
        database.ensureDefaultChannel(QStringLiteral("-1001234567890"), &error);
    QVERIFY2(channelId > 0, qPrintable(error));

    const QDateTime planned = QDateTime::currentDateTimeUtc().addSecs(3600);
    const qint64 first = database.addVideo(QStringLiteral("https://tiktok.com/one"), 1, 1,
                                           channelId, planned, &error);
    const qint64 second = database.addVideo(QStringLiteral("https://tiktok.com/two"), 1, 1,
                                            channelId, planned, &error);
    QVERIFY2(first > 0 && second > 0, qPrintable(error));
    QVERIFY2(database.moveVideo(second, -1, &error), qPrintable(error));
    QCOMPARE(database.activeVideos().first().id, second);

    QVERIFY2(database.markFailed(second, QStringLiteral("network error"), &error),
             qPrintable(error));
    QCOMPARE(database.failedVideos().first().retryCount, 1);
    QVERIFY2(database.retryVideo(second, &error), qPrintable(error));
    QCOMPARE(database.video(second)->status, QStringLiteral("queued"));

    QVERIFY2(
        database.markReady(second, directory.filePath(QStringLiteral("gone.mp4")), 100, &error),
        qPrintable(error));
    QVERIFY2(database.requeueForDownload(second, &error), qPrintable(error));
    QCOMPARE(database.video(second)->status, QStringLiteral("queued"));
    QVERIFY(database.video(second)->localFilePath.isEmpty());

    QVERIFY2(database.cancelVideo(first, &error), qPrintable(error));
    QCOMPARE(database.video(first)->status, QStringLiteral("cancelled"));
    QVERIFY2(database.retryVideo(first, &error), qPrintable(error));
    QCOMPARE(database.video(first)->status, QStringLiteral("queued"));
}

void TestAppDatabase::persistsServerScheduledLifecycle()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    AppDatabase database(directory.filePath(QStringLiteral("app.sqlite")));
    QString error;
    QVERIFY2(database.open(&error), qPrintable(error));
    const qint64 channelId =
        database.ensureDefaultChannel(QStringLiteral("@server_queue_channel"), &error);
    QVERIFY2(channelId > 0, qPrintable(error));

    const QDateTime planned = QDateTime::currentDateTimeUtc().addSecs(3600);
    const qint64 videoId = database.addVideo(QStringLiteral("https://tiktok.com/server-scheduled"),
                                             1, 2, channelId, planned, &error);
    QVERIFY2(videoId > 0, qPrintable(error));
    QVERIFY2(database.markReady(videoId, QStringLiteral("C:/cache/video.mp4"), 5'000'000, &error),
             qPrintable(error));
    QVERIFY(database.nextVideoForServerSubmission().has_value());
    QVERIFY2(database.markUploading(videoId, &error), qPrintable(error));
    QVERIFY2(database.markServerScheduled(videoId, QStringLiteral("-100777"),
                                          QStringLiteral("123456"), &error),
             qPrintable(error));

    const std::optional<VideoRecord> scheduled = database.video(videoId);
    QVERIFY(scheduled.has_value());
    QCOMPARE(scheduled->status, QStringLiteral("server_scheduled"));
    QCOMPARE(scheduled->publisherMode, QStringLiteral("tdlib"));
    QCOMPARE(scheduled->tdChatId, QStringLiteral("-100777"));
    QCOMPARE(scheduled->tdMessageId, QStringLiteral("123456"));
    QVERIFY(scheduled->localFilePath.isEmpty());
    QCOMPARE(database.activeVideos().size(), 1);

    const QDateTime rescheduled = planned.addSecs(600);
    QVERIFY2(database.updateServerSchedule(videoId, rescheduled, &error), qPrintable(error));
    QCOMPARE(database.video(videoId)->scheduledAtUtc, rescheduled);
    QVERIFY2(database.finalizeElapsedServerSchedules(planned, &error), qPrintable(error));
    QCOMPARE(database.video(videoId)->status, QStringLiteral("server_scheduled"));
    QVERIFY2(database.finalizeElapsedServerSchedules(rescheduled.addSecs(1), &error),
             qPrintable(error));
    QCOMPARE(database.video(videoId)->status, QStringLiteral("server_scheduled"));
    QVERIFY(database.markServerPublished(videoId, QStringLiteral("-100777"),
                                         QStringLiteral("654321"), rescheduled));
    QCOMPARE(database.video(videoId)->status, QStringLiteral("sent"));
}

void TestAppDatabase::migratesVersionOneSchema()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("legacy.sqlite"));
    const QString connectionName = QStringLiteral("legacy-schema-test");
    {
        QSqlDatabase legacy = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        legacy.setDatabaseName(path);
        QVERIFY(legacy.open());
        QSqlQuery query(legacy);
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE channels(id INTEGER PRIMARY KEY AUTOINCREMENT,name TEXT NOT NULL,"
            "telegram_id TEXT NOT NULL UNIQUE,is_default INTEGER NOT NULL DEFAULT 0,"
            "enabled INTEGER NOT NULL DEFAULT 1)")));
        QVERIFY(query.exec(
            QStringLiteral("INSERT INTO channels(id,name,telegram_id,is_default,enabled) "
                           "VALUES(1,'Legacy','-1001234567890',1,1)")));
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE videos(id INTEGER PRIMARY KEY AUTOINCREMENT,url TEXT NOT NULL,"
            "channel_id INTEGER NOT NULL,status TEXT NOT NULL,request_chat_id TEXT NOT NULL,"
            "request_user_id TEXT NOT NULL,requested_at_utc TEXT NOT NULL,"
            "scheduled_at_utc TEXT,published_at_utc TEXT,title TEXT,author TEXT,"
            "thumbnail_path TEXT,local_file_path TEXT,error TEXT,duration_seconds INTEGER "
            "NOT NULL DEFAULT 0,file_size INTEGER NOT NULL DEFAULT 0,retry_count INTEGER "
            "NOT NULL DEFAULT 0,queue_order INTEGER NOT NULL,progress REAL NOT NULL DEFAULT 0,"
            "force_publish INTEGER NOT NULL DEFAULT 0)")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO videos(url,channel_id,status,request_chat_id,request_user_id,"
            "requested_at_utc,scheduled_at_utc,queue_order) VALUES("
            "'https://tiktok.com/legacy',1,'queued','1','2',"
            "'2026-08-25T00:00:00.000Z','2026-08-25T02:00:00.000Z',1)")));
        QVERIFY(query.exec(QStringLiteral("PRAGMA user_version=1")));
        legacy.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    AppDatabase migrated(path);
    QString error;
    QVERIFY2(migrated.open(&error), qPrintable(error));
    QCOMPARE(migrated.activeVideos().size(), 1);
    QCOMPARE(migrated.activeVideos().first().publisherMode, QStringLiteral("local"));
    QVERIFY(migrated.activeVideos().first().tdChatId.isEmpty());
    QVERIFY(migrated.activeVideos().first().tdMessageId.isEmpty());
}

void TestAppDatabase::exportsAndImportsBackup()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString backupPath = directory.filePath(QStringLiteral("backup.json"));
    QString error;

    AppDatabase source(directory.filePath(QStringLiteral("source.sqlite")));
    QVERIFY2(source.open(&error), qPrintable(error));
    const qint64 channelId = source.ensureDefaultChannel(QStringLiteral("-1001234567890"), &error);
    QVERIFY2(channelId > 0, qPrintable(error));
    QVERIFY2(source.addScheduleSlot(channelId, 0, QStringLiteral("12:00"), &error) > 0,
             qPrintable(error));
    const qint64 videoId = source.addVideo(QStringLiteral("https://tiktok.com/backup"), 1, 2,
                                           channelId, QDateTime::currentDateTimeUtc(), &error);
    QVERIFY2(videoId > 0, qPrintable(error));
    QVERIFY2(source.markReady(videoId, QStringLiteral("C:/untrusted/video.mp4"), 5000, &error),
             qPrintable(error));
    QVERIFY2(source.exportBackup(backupPath, &error), qPrintable(error));
    QVERIFY(QFileInfo::exists(backupPath));

    AppDatabase restored(directory.filePath(QStringLiteral("restored.sqlite")));
    QVERIFY2(restored.open(&error), qPrintable(error));
    QVERIFY2(restored.importBackup(backupPath, &error), qPrintable(error));
    QCOMPARE(restored.channels().size(), 1);
    QVERIFY(restored.defaultChannel().has_value());
    QCOMPARE(restored.scheduleSlots().size(), 1);
    QCOMPARE(restored.activeVideos().size(), 1);
    QCOMPARE(restored.activeVideos().first().url, QStringLiteral("https://tiktok.com/backup"));
    QCOMPARE(restored.activeVideos().first().status, QStringLiteral("queued"));
    QVERIFY(restored.activeVideos().first().localFilePath.isEmpty());
}

QTEST_GUILESS_MAIN(TestAppDatabase)

#include "TestAppDatabase.moc"
