/******************************************************************************
 * @file    TestTdJsonClient.cpp
 * @brief   Tests dynamic TDLib loading, request delivery, receive, and shutdown.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#include "telegram/TdJsonClient.h"

#include <QSignalSpy>
#include <QTest>

class TestTdJsonClient final : public QObject
{
    Q_OBJECT

private slots:
    void rejectsRequestsWhileStopped();
    void loadsSendsReceivesAndStops();
};

void TestTdJsonClient::rejectsRequestsWhileStopped()
{
    TdJsonClient client;
    QString error;
    QVERIFY(!client.send(QJsonObject{{QStringLiteral("@type"), QStringLiteral("getMe")}}, &error));
    QVERIFY(error.contains(QStringLiteral("not running"), Qt::CaseInsensitive));
    QVERIFY(!client.isRunning());
}

void TestTdJsonClient::loadsSendsReceivesAndStops()
{
    TdJsonClient client;
    QSignalSpy eventSpy(&client, &TdJsonClient::eventReceived);
    QSignalSpy errorSpy(&client, &TdJsonClient::receiveError);
    QString error;
    QVERIFY2(client.start(QString::fromUtf8(TEST_TDJSON_PATH), &error), qPrintable(error));
    QVERIFY(client.isRunning());
    QVERIFY(!client.loadedLibraryPath().isEmpty());
    QVERIFY(client.start(QString::fromUtf8(TEST_TDJSON_PATH), &error));

    QTRY_VERIFY_WITH_TIMEOUT(eventSpy.size() >= 1, 2000);
    QJsonObject event = eventSpy.constFirst().constFirst().toJsonObject();
    QCOMPARE(event.value(QStringLiteral("@type")).toString(),
             QStringLiteral("updateAuthorizationState"));
    QCOMPARE(event.value(QStringLiteral("authorization_state"))
                 .toObject()
                 .value(QStringLiteral("@type"))
                 .toString(),
             QStringLiteral("authorizationStateWaitTdlibParameters"));

    const QJsonObject request{{QStringLiteral("@type"), QStringLiteral("setTdlibParameters")},
                              {QStringLiteral("database_directory"), QStringLiteral("test")}};
    QVERIFY2(client.send(request, &error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(eventSpy.size() >= 2, 2000);
    event = eventSpy.constLast().constFirst().toJsonObject();
    QCOMPARE(event.value(QStringLiteral("authorization_state"))
                 .toObject()
                 .value(QStringLiteral("@type"))
                 .toString(),
             QStringLiteral("authorizationStateWaitPhoneNumber"));
    QCOMPARE(errorSpy.size(), 0);

    client.stop();
    client.stop();
    QVERIFY(!client.isRunning());
    error.clear();
    QVERIFY(!client.send(request, &error));
    QVERIFY(error.contains(QStringLiteral("not running"), Qt::CaseInsensitive));
}

QTEST_GUILESS_MAIN(TestTdJsonClient)

#include "TestTdJsonClient.moc"
