#include "ui/SettingsController.h"
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

class TestDesktopWorkflow final : public QObject
{
    Q_OBJECT
private slots:
    void batchPreviewApprovalAndBulkActions()
    {
        QTemporaryDir directory;
        SettingsController controller(nullptr, directory.filePath("test.sqlite"));
        controller.addChannel("Testing", "@test_channel");
        QVERIFY(!controller.statusIsError());
        const auto channel = controller.channels().first().toMap().value("id").toLongLong();
        controller.setChannelTimeZone(channel, "Europe/Kyiv");
        controller.setChannelCaptionTemplate(channel, "{title}");
        const QString first = "https://www.tiktok.com/@creator/video/1234567890";
        const QString second = "https://www.tiktok.com/@creator/video/1234567891";
        const QString batch = first + "\n" + second + "\n" + first + "\nhttps://example.org/video";
        const auto preview = controller.previewUrls(batch, channel, false);
        QCOMPARE(preview.size(), 4);
        QVERIFY(preview[0].toMap().value("valid").toBool());
        QVERIFY(!preview[2].toMap().value("valid").toBool());
        const auto added = controller.addVideos(batch, channel, {}, true, false);
        QCOMPARE(added.value("added").toInt(), 2);
        QCOMPARE(added.value("errors").toStringList().size(), 2);
        const auto videos = controller.scheduledVideos();
        QVERIFY(!videos.first().toMap().value("approved").toBool());
        QCOMPARE(videos.first().toMap().value("caption").toString(), QStringLiteral("{title}"));
        const auto one = videos[0].toMap().value("id");
        const auto two = videos[1].toMap().value("id");
        const auto bulk =
            controller.bulkVideoAction({one, two, two, QVariant(9999)}, "approve", {});
        QCOMPARE(bulk.value("changed").toInt(), 2);
        QCOMPARE(bulk.value("errors").toStringList().size(), 1);
        QVERIFY(controller.scheduledVideos().first().toMap().value("approved").toBool());
        QCOMPARE(controller.addVideos(first, channel, {}, false, false).value("added").toInt(), 0);
        QCOMPARE(controller.addVideos(first, channel, {}, false, true).value("added").toInt(), 1);
        controller.moveVideoToDate(one.toLongLong(),
                                   QDate::currentDate().addDays(2).toString(Qt::ISODate));
        QVERIFY2(!controller.statusIsError(), qPrintable(controller.statusMessage()));
        QVERIFY(controller.videoEvents(one.toLongLong()).size() >= 3);
        controller.setVideoCaption(one.toLongLong(), QString(1025, 'a'));
        QVERIFY(controller.statusIsError());
    }
    void rejectsOversizedBatchAndInvalidDate()
    {
        QTemporaryDir directory;
        SettingsController controller(nullptr, directory.filePath("test.sqlite"));
        controller.addChannel("Testing", "@test_channel");
        const auto channel = controller.channels().first().toMap().value("id").toLongLong();
        QCOMPARE(controller.addVideos(QString(256001, 'x'), channel, {}, true, false)
                     .value("added")
                     .toInt(),
                 0);
        QCOMPARE(
            controller.addVideos("https://vm.tiktok.com/ABC123/", channel, "invalid", true, false)
                .value("added")
                .toInt(),
            0);
        QVERIFY(controller.scheduledVideos().isEmpty());
    }
};
int main(int argc, char** argv)
{
    Q_UNUSED(argc)
    char smoke[] = "--ui-smoke-test";
    char* arguments[]{argv[0], smoke, nullptr};
    int count = 2;
    QCoreApplication application(count, arguments);
    QTemporaryDir directory;
    QCoreApplication::setOrganizationName("TikTokTelegramBotTests");
    QCoreApplication::setApplicationName("DesktopWorkflow");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    TestDesktopWorkflow test;
    return QTest::qExec(&test, QStringList{QString::fromLocal8Bit(argv[0])});
}
#include "TestDesktopWorkflow.moc"
