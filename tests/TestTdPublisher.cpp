/******************************************************************************
 * @file    TestTdPublisher.cpp
 * @brief   Tests TDLib authorization and scheduled-message request flow.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#include "telegram/TdPublisher.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TestTdPublisher final : public QObject
{
    Q_OBJECT

private slots:
    void authorizesSchedulesAndEditsServerPost();
};

void TestTdPublisher::authorizesSchedulesAndEditsServerPost()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString videoPath = directory.filePath(QStringLiteral("video.mp4"));
    QFile video(videoPath);
    QVERIFY(video.open(QIODevice::WriteOnly));
    QCOMPARE(video.write("fake-video"), 10LL);
    video.close();

    TdPublisher publisher;
    QSignalSpy acceptedSpy(&publisher, &TdPublisher::videoAccepted);
    QSignalSpy operationSpy(&publisher, &TdPublisher::remoteOperationFinished);
    QSignalSpy reconciledSpy(&publisher, &TdPublisher::videoReconciled);
    TdPublisherConfig config;
    config.libraryPath = QString::fromUtf8(TEST_TDJSON_PATH);
    config.apiId = 12345;
    config.apiHash = QString(32, QLatin1Char('a'));
    config.databaseEncryptionKey = QByteArray(32, 'k');
    config.databaseDirectory = directory.filePath(QStringLiteral("tdlib"));
    QString error;

    QVERIFY(!publisher.submitVideo(70, QStringLiteral("@channel_name"), videoPath,
                                   QDateTime::currentDateTimeUtc().addSecs(3600), 30, false,
                                   &error));
    QVERIFY(error.contains(QStringLiteral("not authorized"), Qt::CaseInsensitive));

    QVERIFY2(publisher.start(config, &error), qPrintable(error));

    QTRY_COMPARE_WITH_TIMEOUT(publisher.authorizationState(), QStringLiteral("wait_phone"), 2000);
    publisher.submitPhoneNumber(QStringLiteral("+380000000000"));
    QTRY_COMPARE_WITH_TIMEOUT(publisher.authorizationState(), QStringLiteral("wait_code"), 2000);
    publisher.submitAuthenticationCode(QStringLiteral("12345"));
    QTRY_VERIFY_WITH_TIMEOUT(publisher.isReady(), 2000);

    const QDateTime planned = QDateTime::currentDateTimeUtc().addSecs(3600);
    error.clear();
    QVERIFY(!publisher.submitVideo(71, QStringLiteral("@channel_name"),
                                   directory.filePath(QStringLiteral("missing.mp4")), planned, 30,
                                   false, &error));
    QVERIFY(error.contains(QStringLiteral("does not exist"), Qt::CaseInsensitive));

    error.clear();
    QVERIFY(!publisher.submitVideo(72, QStringLiteral("@channel_name"), videoPath,
                                   QDateTime::currentDateTimeUtc().addSecs(1), 30, false, &error));
    QVERIFY(error.contains(QStringLiteral("10 seconds"), Qt::CaseInsensitive));

    error.clear();
    QVERIFY(!publisher.cancelScheduledVideo(73, QString(), QStringLiteral("123456"), &error));
    QVERIFY(error.contains(QStringLiteral("identifier"), Qt::CaseInsensitive));

    QVERIFY2(publisher.submitVideo(77, QStringLiteral("@channel_name"), videoPath, planned, 30,
                                   false, &error),
             qPrintable(error));
    QTRY_COMPARE_WITH_TIMEOUT(acceptedSpy.size(), 1, 2000);
    const QList<QVariant> accepted = acceptedSpy.takeFirst();
    QCOMPARE(accepted.at(0).toLongLong(), 77LL);
    QCOMPARE(accepted.at(1).toString(), QStringLiteral("-100777"));
    QCOMPARE(accepted.at(2).toString(), QStringLiteral("123456"));
    QVERIFY(accepted.at(3).toBool());

    for (const auto& check : QList<QPair<QString, QString>>{{"123456", "scheduled"},
                                                            {"654321", "published"},
                                                            {"999", "unknown"},
                                                            {"888", "unknown"}})
    {
        QVERIFY2(publisher.reconcileVideo(77, "-100777", check.first, &error), qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(reconciledSpy.size(), 1, 2000);
        const auto result = reconciledSpy.takeFirst();
        QCOMPARE(result.at(0).toLongLong(), 77LL);
        QCOMPARE(result.at(1).toString(), check.second);
    }

    QVERIFY2(publisher.rescheduleVideo(77, QStringLiteral("-100777"), QStringLiteral("123456"),
                                       planned.addSecs(600), &error),
             qPrintable(error));
    QTRY_COMPARE_WITH_TIMEOUT(operationSpy.size(), 1, 2000);
    const QList<QVariant> operation = operationSpy.takeFirst();
    QCOMPARE(operation.at(0).toLongLong(), 77LL);
    QCOMPARE(operation.at(1).toString(), QStringLiteral("reschedule"));
    QVERIFY(operation.at(2).toBool());

    QVERIFY2(publisher.cancelScheduledVideo(78, QStringLiteral("-100777"), QStringLiteral("123456"),
                                            &error),
             qPrintable(error));
    QTRY_COMPARE_WITH_TIMEOUT(operationSpy.size(), 1, 2000);
    const QList<QVariant> cancellation = operationSpy.takeFirst();
    QCOMPARE(cancellation.at(0).toLongLong(), 78LL);
    QCOMPARE(cancellation.at(1).toString(), QStringLiteral("cancel"));
    QVERIFY(cancellation.at(2).toBool());

    QVERIFY2(publisher.publishScheduledVideoNow(79, QStringLiteral("-100777"),
                                                QStringLiteral("123456"), &error),
             qPrintable(error));
    QTRY_COMPARE_WITH_TIMEOUT(operationSpy.size(), 1, 2000);
    const QList<QVariant> publication = operationSpy.takeFirst();
    QCOMPARE(publication.at(0).toLongLong(), 79LL);
    QCOMPARE(publication.at(1).toString(), QStringLiteral("publish_now"));
    QVERIFY(publication.at(2).toBool());
    publisher.stop();
}

QTEST_GUILESS_MAIN(TestTdPublisher)

#include "TestTdPublisher.moc"
