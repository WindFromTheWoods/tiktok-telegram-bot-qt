/******************************************************************************
 * @file    TestTelegramApiResponse.cpp
 * @brief   Tests normalization of Telegram Bot API responses and errors.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

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
    void rejectsNonObjectJson();
    void treatsHttpFailureAsFailure();
    void networkErrorOverridesSuccessfulPayload();
    void suppliesFallbackDescriptions();
    void requiresDeliveryEvidence();
};

void TestTelegramApiResponse::parsesSuccess()
{
    const TelegramApiResponse response =
        TelegramApi::parseResponse(R"({"ok":true,"result":{"id":123}})", 200);

    QVERIFY(response.ok);
    QCOMPARE(response.result.toObject().value(QStringLiteral("id")).toInt(), 123);
}

void TestTelegramApiResponse::rejectsNonObjectJson()
{
    const TelegramApiResponse response = TelegramApi::parseResponse(R"([true, 123])", 200);

    QVERIFY(!response.ok);
    QVERIFY(response.description.contains(QStringLiteral("malformed JSON")));
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
    QCOMPARE(response.errorCode, 502);
    QCOMPARE(response.description, QStringLiteral("Telegram request failed with HTTP status 502"));
}

void TestTelegramApiResponse::networkErrorOverridesSuccessfulPayload()
{
    const TelegramApiResponse response = TelegramApi::parseResponse(
        R"({"ok":true,"result":true})", 200, QStringLiteral("connection reset"));

    QVERIFY(!response.ok);
    QCOMPARE(response.description,
             QStringLiteral("Telegram network request failed: connection reset"));
}

void TestTelegramApiResponse::suppliesFallbackDescriptions()
{
    const TelegramApiResponse apiFailure = TelegramApi::parseResponse(R"({"ok":false})", 0);
    QVERIFY(!apiFailure.ok);
    QCOMPARE(apiFailure.description, QStringLiteral("Telegram API request failed"));
    QCOMPARE(apiFailure.retryAfterSeconds, 0);

    const TelegramApiResponse networkFailure =
        TelegramApi::parseResponse(R"({"ok":false})", 0, QStringLiteral("host not found"));
    QVERIFY(!networkFailure.ok);
    QCOMPARE(networkFailure.description,
             QStringLiteral("Telegram network request failed: host not found"));
}

void TestTelegramApiResponse::requiresDeliveryEvidence()
{
    const auto confirmed = TelegramApi::uploadResult(TelegramApi::parseResponse(
        R"({"ok":true,"result":{"message_id":123,"chat":{"id":-100777}}})", 200));
    QVERIFY(confirmed.success);
    QCOMPARE(confirmed.messageId, QStringLiteral("123"));
    QCOMPARE(confirmed.chatId, QStringLiteral("-100777"));
    for (const QByteArray& payload :
         {QByteArray("broken"), QByteArray(R"({"ok":true})"),
          QByteArray(R"({"ok":true,"result":{"message_id":1e50,"chat":{"id":-100777}}})")})
    {
        const auto result = TelegramApi::uploadResult(TelegramApi::parseResponse(payload, 200));
        QVERIFY(!result.success);
        QVERIFY(result.deliveryUnknown);
    }
    QVERIFY(TelegramApi::uploadResult(TelegramApi::parseResponse(R"({"ok":true})", 403))
                .deliveryUnknown);
    QVERIFY(TelegramApi::uploadResult(
                TelegramApi::parseResponse(R"({"ok":false,"error_code":500})", 500))
                .deliveryUnknown);
    const auto limited = TelegramApi::uploadResult(TelegramApi::parseResponse(
        R"({"ok":false,"error_code":429,"parameters":{"retry_after":47}})", 429));
    QVERIFY(!limited.deliveryUnknown);
    QCOMPARE(limited.retryAfterSeconds, 47);
    QCOMPARE(limited.category, QStringLiteral("rate_limit"));
}

QTEST_APPLESS_MAIN(TestTelegramApiResponse)

#include "TestTelegramApiResponse.moc"
