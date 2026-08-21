#include "telegram/TelegramApi.h"

#include <QJsonObject>
#include <QTest>

class TestTelegramApiResponse final : public QObject
{
    Q_OBJECT

private slots:
    void parsesSuccess();
    void parsesApiErrorAndRetryDelay();
    void rejectsMalformedJson();
    void treatsHttpFailureAsFailure();
};

void TestTelegramApiResponse::parsesSuccess()
{
    const TelegramApiResponse response =
        TelegramApi::parseResponse(R"({"ok":true,"result":{"id":123}})", 200);

    QVERIFY(response.ok);
    QCOMPARE(response.result.toObject().value(QStringLiteral("id")).toInt(), 123);
}

void TestTelegramApiResponse::parsesApiErrorAndRetryDelay()
{
    const TelegramApiResponse response = TelegramApi::parseResponse(
        R"({"ok":false,"error_code":429,"description":"Too Many Requests","parameters":{"retry_after":7}})",
        429);

    QVERIFY(!response.ok);
    QCOMPARE(response.errorCode, 429);
    QCOMPARE(response.description, QStringLiteral("Too Many Requests"));
    QCOMPARE(response.retryAfterSeconds, 7);
}

void TestTelegramApiResponse::rejectsMalformedJson()
{
    const TelegramApiResponse response = TelegramApi::parseResponse("not-json", 200);

    QVERIFY(!response.ok);
    QVERIFY(response.description.contains(QStringLiteral("malformed JSON")));
}

void TestTelegramApiResponse::treatsHttpFailureAsFailure()
{
    const TelegramApiResponse response =
        TelegramApi::parseResponse(R"({"ok":true,"result":true})", 502);

    QVERIFY(!response.ok);
}

QTEST_APPLESS_MAIN(TestTelegramApiResponse)

#include "TestTelegramApiResponse.moc"
