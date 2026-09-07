/******************************************************************************
 * @file    TestAppDatabaseValidation.cpp
 * @brief   Tests SQLite input validation, recovery, and rejected state transitions.
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

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

class TestAppDatabaseValidation final : public QObject
{
    Q_OBJECT

private slots:
    void validatesChannelsAndScheduleSlots();
    void clampsProgressAndMetadata();
    void recoversInterruptedStates();
    void rejectsInvalidVideoTransitions();
    void rejectsInvalidBackupWithoutChangingData();
};

void TestAppDatabaseValidation::validatesChannelsAndScheduleSlots()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    AppDatabase database(directory.filePath(QStringLiteral("validation.sqlite")));
    QString error;
    QVERIFY2(database.open(&error), qPrintable(error));

    QCOMPARE(database.ensureDefaultChannel(QStringLiteral("12345"), &error), 0LL);
    QVERIFY(error.contains(QStringLiteral("invalid"), Qt::CaseInsensitive));

    const qint64 defaultChannel =
        database.ensureDefaultChannel(QStringLiteral("-1001234567890"), &error);
    QVERIFY2(defaultChannel > 0, qPrintable(error));
    QVERIFY(!database.disableChannel(defaultChannel, &error));

    const qint64 secondary =
        database.addChannel(QStringLiteral("Secondary"), QStringLiteral("@secondary"), &error);
    QVERIFY2(secondary > 0, qPrintable(error));
    const qint64 videoId =
        database.addVideo(QStringLiteral("https://tiktok.com/active"), 1, 2, secondary,
                          QDateTime::currentDateTimeUtc().addSecs(3600), &error);
    QVERIFY2(videoId > 0, qPrintable(error));
    QVERIFY(!database.disableChannel(secondary, &error));
    QVERIFY2(database.cancelVideo(videoId, &error), qPrintable(error));
    QVERIFY2(database.disableChannel(secondary, &error), qPrintable(error));

    const QList<ChannelRecord> allChannels = database.channels(true);
    const auto disabled =
        std::find_if(allChannels.cbegin(), allChannels.cend(),
                     [secondary](const ChannelRecord& channel) { return channel.id == secondary; });
    QVERIFY(disabled != allChannels.cend());
    QVERIFY(!disabled->enabled);

    QCOMPARE(database.addScheduleSlot(defaultChannel, -1, QStringLiteral("12:00"), &error), 0LL);
    QCOMPARE(database.addScheduleSlot(defaultChannel, 8, QStringLiteral("12:00"), &error), 0LL);
    QCOMPARE(database.addScheduleSlot(defaultChannel, 0, QStringLiteral("25:00"), &error), 0LL);
    const qint64 slotId =
        database.addScheduleSlot(defaultChannel, 7, QStringLiteral(" 08:05 "), &error);
    QVERIFY2(slotId > 0, qPrintable(error));
    QCOMPARE(database.scheduleSlots(defaultChannel).constFirst().time, QStringLiteral("08:05"));
    QVERIFY2(database.removeScheduleSlot(slotId, &error), qPrintable(error));
    QVERIFY(database.scheduleSlots(defaultChannel).isEmpty());
}

void TestAppDatabaseValidation::clampsProgressAndMetadata()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    AppDatabase database(directory.filePath(QStringLiteral("metadata.sqlite")));
    QString error;
    QVERIFY2(database.open(&error), qPrintable(error));
    const qint64 channelId =
        database.ensureDefaultChannel(QStringLiteral("-1001234567890"), &error);
    const qint64 videoId =
        database.addVideo(QStringLiteral("https://tiktok.com/metadata"), 1, 2, channelId,
                          QDateTime::currentDateTimeUtc().addSecs(3600), &error);
    QVERIFY2(videoId > 0, qPrintable(error));
    QVERIFY2(database.markDownloading(videoId, &error), qPrintable(error));

    QVERIFY2(database.updateDownloadProgress(videoId, -25.0, &error), qPrintable(error));
    QCOMPARE(database.video(videoId)->progress, 0.0);
    QVERIFY2(database.updateDownloadProgress(videoId, 250.0, &error), qPrintable(error));
    QCOMPARE(database.video(videoId)->progress, 100.0);

    const QString longTitle(600, QLatin1Char('t'));
    const QString longAuthor(300, QLatin1Char('a'));
    QVERIFY2(database.updateMetadata(videoId, longTitle, longAuthor, -50, QString(), &error),
             qPrintable(error));
    const VideoRecord metadata = *database.video(videoId);
    QCOMPARE(metadata.title.size(), 500);
    QCOMPARE(metadata.author.size(), 250);
    QCOMPARE(metadata.durationSeconds, 0);

    const QString longError(2500, QLatin1Char('e'));
    QVERIFY2(database.markFailed(videoId, longError, &error), qPrintable(error));
    QCOMPARE(database.video(videoId)->error.size(), 2000);
    QCOMPARE(database.video(videoId)->retryCount, 1);
}

void TestAppDatabaseValidation::recoversInterruptedStates()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    AppDatabase database(directory.filePath(QStringLiteral("recovery.sqlite")));
    QString error;
    QVERIFY2(database.open(&error), qPrintable(error));
    const qint64 channelId =
        database.ensureDefaultChannel(QStringLiteral("-1001234567890"), &error);
    const QDateTime planned = QDateTime::currentDateTimeUtc().addSecs(3600);
    const qint64 downloading = database.addVideo(QStringLiteral("https://tiktok.com/downloading"),
                                                 1, 2, channelId, planned, &error);
    const qint64 uploading = database.addVideo(QStringLiteral("https://tiktok.com/uploading"), 1, 2,
                                               channelId, planned, &error);
    QVERIFY2(downloading > 0 && uploading > 0, qPrintable(error));
    QVERIFY2(database.markDownloading(downloading, &error), qPrintable(error));
    QVERIFY2(
        database.markReady(uploading, directory.filePath(QStringLiteral("video.mp4")), 100, &error),
        qPrintable(error));
    QVERIFY2(database.markUploading(uploading, &error), qPrintable(error));

    QVERIFY2(database.recoverInterruptedVideos(&error), qPrintable(error));
    QCOMPARE(database.video(downloading)->status, QStringLiteral("queued"));
    QCOMPARE(database.video(downloading)->progress, 0.0);
    QCOMPARE(database.video(uploading)->status, QStringLiteral("delivery_unknown"));
    QCOMPARE(database.video(uploading)->progress, 100.0);
}

void TestAppDatabaseValidation::rejectsInvalidVideoTransitions()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    AppDatabase database(directory.filePath(QStringLiteral("transitions.sqlite")));
    QString error;
    QVERIFY2(database.open(&error), qPrintable(error));
    const qint64 channelId =
        database.ensureDefaultChannel(QStringLiteral("@server_channel"), &error);
    const qint64 videoId =
        database.addVideo(QStringLiteral("https://tiktok.com/transitions"), 1, 2, channelId,
                          QDateTime::currentDateTimeUtc().addSecs(3600), &error);
    QVERIFY2(videoId > 0, qPrintable(error));

    QVERIFY(!database.moveVideo(videoId, 0, &error));
    QVERIFY(!database.setVideoSchedule(videoId, QDateTime(), &error));
    QVERIFY(!database.retryVideo(videoId, &error));
    QVERIFY(!database.markServerScheduled(videoId, QStringLiteral("-100777"),
                                          QStringLiteral("123456"), &error));

    QVERIFY2(
        database.markReady(videoId, directory.filePath(QStringLiteral("video.mp4")), 100, &error),
        qPrintable(error));
    QVERIFY2(database.markUploading(videoId, &error), qPrintable(error));
    QVERIFY(!database.markServerScheduled(videoId, QString(), QStringLiteral("123456"), &error));
    QVERIFY2(database.markServerScheduled(videoId, QStringLiteral("-100777"),
                                          QStringLiteral("123456"), &error),
             qPrintable(error));
    QVERIFY(!database.setVideoSchedule(videoId, QDateTime::currentDateTimeUtc(), &error));
    QVERIFY(!database.moveVideo(videoId, -1, &error));

    QVERIFY2(
        database.finalizeElapsedServerSchedules(QDateTime::currentDateTimeUtc().addDays(2), &error),
        qPrintable(error));
    QCOMPARE(database.video(videoId)->status, QStringLiteral("server_scheduled"));
    QVERIFY(!database.publishVideoNow(videoId, &error));
    QVERIFY(!database.requeueForDownload(videoId, &error));
}

void TestAppDatabaseValidation::rejectsInvalidBackupWithoutChangingData()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    AppDatabase database(directory.filePath(QStringLiteral("backup.sqlite")));
    QString error;
    QVERIFY2(database.open(&error), qPrintable(error));
    const qint64 channelId =
        database.ensureDefaultChannel(QStringLiteral("-1001234567890"), &error);
    QVERIFY2(channelId > 0, qPrintable(error));

    const QString invalidPath = directory.filePath(QStringLiteral("invalid.json"));
    QFile invalidFile(invalidPath);
    QVERIFY(invalidFile.open(QIODevice::WriteOnly));
    QCOMPARE(invalidFile.write("{not-json"), 9LL);
    invalidFile.close();
    QVERIFY(!database.importBackup(invalidPath, &error));
    QCOMPARE(database.channels().size(), 1);
    QCOMPARE(database.defaultChannel()->id, channelId);

    const QString unsupportedPath = directory.filePath(QStringLiteral("unsupported.json"));
    QFile unsupportedFile(unsupportedPath);
    QVERIFY(unsupportedFile.open(QIODevice::WriteOnly));
    const QByteArray unsupported =
        QByteArrayLiteral(R"({"format":"TikTokTelegramBotBackup","version":999})");
    QCOMPARE(unsupportedFile.write(unsupported), static_cast<qint64>(unsupported.size()));
    unsupportedFile.close();
    QVERIFY(!database.importBackup(unsupportedPath, &error));
    QCOMPARE(database.channels().size(), 1);
}

QTEST_GUILESS_MAIN(TestAppDatabaseValidation)

#include "TestAppDatabaseValidation.moc"
