#include "app/ScheduleCalculator.h"
#include "storage/AppDatabase.h"
#include <QTemporaryDir>
#include <QTest>

class TestDurableWorkflow final : public QObject
{
    Q_OBJECT
private slots:
    void draftsDuplicatesAndHistory()
    {
        QTemporaryDir directory;
        AppDatabase db(directory.filePath("test.sqlite"));
        QString error;
        QVERIFY2(db.open(&error), qPrintable(error));
        const auto channel = db.ensureDefaultChannel("@test_channel");
        const auto other = db.addChannel("Other", "@other_channel");
        const QString url = "https://www.tiktok.com/@creator/video/1234567890";
        const auto id = db.addVideo(url, 0, 0, channel,
                                    QDateTime::currentDateTimeUtc().addSecs(-60), &error, false);
        QVERIFY(id > 0);
        QVERIFY(db.markReady(id, "test.mp4", 100));
        QVERIFY(!db.nextVideoForServerSubmission());
        QVERIFY(!db.nextVideoForUpload(QDateTime::currentDateTimeUtc()));
        QVERIFY(db.setVideoCaption(id, "{title} — {author}"));
        QVERIFY(db.approveVideo(id));
        QCOMPARE(db.nextVideoForServerSubmission()->id, id);
        QVERIFY(db.markUploading(id));
        QVERIFY(!db.setVideoDraft(id, true));
        QVERIFY(db.markBotPublished(id, "-100777", "123", QDateTime::currentDateTimeUtc()));
        QCOMPARE(db.addVideo(url, 0, 0, channel, QDateTime::currentDateTimeUtc()), 0LL);
        QVERIFY(db.addVideo(url, 0, 0, other, QDateTime::currentDateTimeUtc()) > 0);
        const auto repeated = db.addVideo("https://vm.tiktok.com/ABC123/", 0, 0, channel,
                                          QDateTime::currentDateTimeUtc());
        QVERIFY(db.setVideoSourceId(repeated, "1234567890"));
        QVERIFY(db.hasDuplicateSourceVideo(repeated));
        QVERIFY(db.setVideoAllowDuplicate(repeated, true));
        QVERIFY(!db.hasDuplicateSourceVideo(repeated));
        QVERIFY(db.videoEvents(id).size() >= 5);
        QVERIFY(!db.videoEvents(id).first().toMap().value("time").toString().isEmpty());
    }
    void retrySurvivesRestartAndUnknownNeedsVerification()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("test.sqlite");
        qint64 failed{}, interrupted{};
        const auto deadline = QDateTime::currentDateTimeUtc().addSecs(300);
        {
            AppDatabase db(path);
            QVERIFY(db.open());
            const auto channel = db.ensureDefaultChannel("@test_channel");
            failed = db.addVideo("https://tiktok.com/a", 0, 0, channel, deadline);
            interrupted = db.addVideo("https://tiktok.com/b", 0, 0, channel, deadline);
            QVERIFY(db.markFailed(failed, "rate limited"));
            QVERIFY(db.scheduleRetry(failed, deadline, "rate_limit"));
            QVERIFY(db.markReady(interrupted, "test.mp4", 10));
            QVERIFY(db.markUploading(interrupted));
        }
        AppDatabase db(path);
        QVERIFY(db.open());
        QVERIFY(db.recoverInterruptedVideos());
        QCOMPARE(db.video(interrupted)->status, QStringLiteral("delivery_unknown"));
        QVERIFY(!db.retryVideo(interrupted));
        QVERIFY(!db.scheduleRetry(interrupted, deadline, "network"));
        QCOMPARE(db.video(failed)->nextRetryAtUtc, deadline);
        QVERIFY(db.retryDueVideos(deadline.addSecs(-1)));
        QCOMPARE(db.video(failed)->status, QStringLiteral("failed"));
        QVERIFY(db.retryDueVideos(deadline));
        QCOMPARE(db.video(failed)->status, QStringLiteral("queued"));
        QVERIFY(db.video(failed)->nextRetryAtUtc.isNull());
        QVERIFY(db.resolveDeliveryUnknown(interrupted, false));
        QVERIFY(db.retryVideo(interrupted));
    }
    void backupPreservesApprovalAndRemoteEvidence()
    {
        QTemporaryDir directory;
        AppDatabase source(directory.filePath("source.sqlite"));
        QVERIFY(source.open());
        const auto channel = source.ensureDefaultChannel("@test_channel");
        QVERIFY(source.setChannelTimeZone(channel, "Europe/Kyiv"));
        QVERIFY(source.setChannelCaptionTemplate(channel, "{title}"));
        const auto draft = source.addVideo("https://tiktok.com/draft", 0, 0, channel,
                                           QDateTime::currentDateTimeUtc(), nullptr, false, false,
                                           "draft caption");
        const auto remote = source.addVideo("https://tiktok.com/remote", 0, 0, channel,
                                            QDateTime::currentDateTimeUtc());
        QVERIFY(source.markReady(remote, "local.mp4", 10));
        QVERIFY(source.markUploading(remote));
        QVERIFY(source.markServerScheduled(remote, "-100777", "456"));
        const auto file = directory.filePath("backup.json");
        QVERIFY(source.exportBackup(file));
        AppDatabase restored(directory.filePath("restored.sqlite"));
        QVERIFY(restored.open());
        QString error;
        QVERIFY2(restored.importBackup(file, &error), qPrintable(error));
        QCOMPARE(restored.video(remote)->status, QStringLiteral("server_scheduled"));
        QCOMPARE(restored.video(remote)->tdMessageId, QStringLiteral("456"));
        QVERIFY(!restored.video(draft)->approved);
        QCOMPARE(restored.video(draft)->caption, QStringLiteral("draft caption"));
        QCOMPARE(restored.channels().first().timeZoneId, QStringLiteral("Europe/Kyiv"));
        QCOMPARE(restored.channels().first().captionTemplate, QStringLiteral("{title}"));
    }
    void channelTimeZonesAndDaylightSaving()
    {
        const auto now = QDateTime::fromString("2026-03-28T23:00:00Z", Qt::ISODate);
        ScheduleSlotRecord slot;
        slot.dayOfWeek = 0;
        slot.time = "03:30";
        const auto next = ScheduleCalculator::nextPublication(
            now, {}, {slot}, std::chrono::minutes(1), "Europe/Kyiv");
        // 29 March 03:30 does not exist; use the following valid daily slot.
        QCOMPARE(next, QDateTime::fromString("2026-03-30T00:30:00Z", Qt::ISODate));
    }
};
QTEST_GUILESS_MAIN(TestDurableWorkflow)
#include "TestDurableWorkflow.moc"
