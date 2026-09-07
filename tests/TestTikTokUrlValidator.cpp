/******************************************************************************
 * @file    TestTikTokUrlValidator.cpp
 * @brief   Tests the TikTok URL allow list and unsafe-input rejection.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#include "tiktok/TikTokUrlValidator.h"

#include <QTest>

class TestTikTokUrlValidator final : public QObject
{
    Q_OBJECT

private slots:
    void acceptsSupportedUrls_data();
    void acceptsSupportedUrls();
    void rejectsUnsafeOrUnsupportedUrls_data();
    void rejectsUnsafeOrUnsupportedUrls();
};

void TestTikTokUrlValidator::acceptsSupportedUrls_data()
{
    QTest::addColumn<QString>("url");

    QTest::newRow("www video") << QStringLiteral("https://www.tiktok.com/@user/video/123456789");
    QTest::newRow("apex video") << QStringLiteral("https://tiktok.com/@user/video/123456789");
    QTest::newRow("vm short") << QStringLiteral("https://vm.tiktok.com/ABC123/");
    QTest::newRow("vt short") << QStringLiteral("https://vt.tiktok.com/ABC123/");
    QTest::newRow("query string") << QStringLiteral(
        "https://www.tiktok.com/@user/video/123456789?is_from_webapp=1");
    QTest::newRow("explicit TLS port")
        << QStringLiteral("https://www.tiktok.com:443/@user/video/123456789");
    QTest::newRow("uppercase host")
        << QStringLiteral("https://WWW.TIKTOK.COM/@user/video/123456789");
    QTest::newRow("short code characters") << QStringLiteral("https://vm.tiktok.com/A_b-C9/");
}

void TestTikTokUrlValidator::acceptsSupportedUrls()
{
    QFETCH(QString, url);
    QVERIFY2(TikTokUrlValidator::isValid(url), qPrintable(url));
}

void TestTikTokUrlValidator::rejectsUnsafeOrUnsupportedUrls_data()
{
    QTest::addColumn<QString>("url");

    QTest::newRow("insecure scheme") << QStringLiteral("http://www.tiktok.com/@user/video/123");
    QTest::newRow("wrong host") << QStringLiteral("https://example.com/test");
    QTest::newRow("host suffix attack")
        << QStringLiteral("https://tiktok.com.evil.com/@user/video/123");
    QTest::newRow("query deception") << QStringLiteral("https://evil.com/?url=tiktok.com");
    QTest::newRow("file scheme") << QStringLiteral("file:///tmp/video");
    QTest::newRow("javascript scheme") << QStringLiteral("javascript:alert(1)");
    QTest::newRow("plain text") << QStringLiteral("not a url");
    QTest::newRow("missing user") << QStringLiteral("https://tiktok.com/video/123");
    QTest::newRow("missing video id") << QStringLiteral("https://tiktok.com/@user/video/");
    QTest::newRow("non-numeric video id")
        << QStringLiteral("https://tiktok.com/@user/video/not-a-number");
    QTest::newRow("extra main path") << QStringLiteral("https://tiktok.com/@user/video/123/extra");
    QTest::newRow("empty short code") << QStringLiteral("https://vm.tiktok.com/");
    QTest::newRow("unsupported port") << QStringLiteral("https://tiktok.com:444/@user/video/123");
    QTest::newRow("userinfo") << QStringLiteral("https://attacker@tiktok.com/@user/video/123");
    QTest::newRow("leading whitespace") << QStringLiteral(" https://tiktok.com/@user/video/123");
    QTest::newRow("trailing whitespace") << QStringLiteral("https://tiktok.com/@user/video/123 ");
    QTest::newRow("mobile host") << QStringLiteral("https://m.tiktok.com/@user/video/123");
    QTest::newRow("short code dot") << QStringLiteral("https://vm.tiktok.com/ABC.123/");
    QTest::newRow("encoded path separator")
        << QStringLiteral("https://tiktok.com/@user/video%2F123");
    QTest::newRow("uppercase encoded path separator")
        << QStringLiteral("https://tiktok.com/@user/video%2f123");
    QTest::newRow("empty input") << QString{};
}

void TestTikTokUrlValidator::rejectsUnsafeOrUnsupportedUrls()
{
    QFETCH(QString, url);
    QVERIFY2(!TikTokUrlValidator::isValid(url), qPrintable(url));
}

QTEST_APPLESS_MAIN(TestTikTokUrlValidator)

#include "TestTikTokUrlValidator.moc"
