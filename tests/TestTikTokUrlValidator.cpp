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

    QTest::newRow("www video") << QStringLiteral(
        "https://www.tiktok.com/@user/video/123456789");
    QTest::newRow("apex video") << QStringLiteral(
        "https://tiktok.com/@user/video/123456789");
    QTest::newRow("vm short") << QStringLiteral("https://vm.tiktok.com/ABC123/");
    QTest::newRow("vt short") << QStringLiteral("https://vt.tiktok.com/ABC123/");
    QTest::newRow("query string") << QStringLiteral(
        "https://www.tiktok.com/@user/video/123456789?is_from_webapp=1");
    QTest::newRow("explicit TLS port") << QStringLiteral(
        "https://www.tiktok.com:443/@user/video/123456789");
}

void TestTikTokUrlValidator::acceptsSupportedUrls()
{
    QFETCH(QString, url);
    QVERIFY2(TikTokUrlValidator::isValid(url), qPrintable(url));
}

void TestTikTokUrlValidator::rejectsUnsafeOrUnsupportedUrls_data()
{
    QTest::addColumn<QString>("url");

    QTest::newRow("insecure scheme") << QStringLiteral(
        "http://www.tiktok.com/@user/video/123");
    QTest::newRow("wrong host") << QStringLiteral("https://example.com/test");
    QTest::newRow("host suffix attack") << QStringLiteral(
        "https://tiktok.com.evil.com/@user/video/123");
    QTest::newRow("query deception") << QStringLiteral(
        "https://evil.com/?url=tiktok.com");
    QTest::newRow("file scheme") << QStringLiteral("file:///tmp/video");
    QTest::newRow("javascript scheme") << QStringLiteral("javascript:alert(1)");
    QTest::newRow("plain text") << QStringLiteral("not a url");
    QTest::newRow("missing user") << QStringLiteral("https://tiktok.com/video/123");
    QTest::newRow("missing video id") << QStringLiteral(
        "https://tiktok.com/@user/video/");
    QTest::newRow("non-numeric video id") << QStringLiteral(
        "https://tiktok.com/@user/video/not-a-number");
    QTest::newRow("extra main path") << QStringLiteral(
        "https://tiktok.com/@user/video/123/extra");
    QTest::newRow("empty short code") << QStringLiteral("https://vm.tiktok.com/");
    QTest::newRow("unsupported port") << QStringLiteral(
        "https://tiktok.com:444/@user/video/123");
    QTest::newRow("userinfo") << QStringLiteral(
        "https://attacker@tiktok.com/@user/video/123");
    QTest::newRow("leading whitespace") << QStringLiteral(
        " https://tiktok.com/@user/video/123");
}

void TestTikTokUrlValidator::rejectsUnsafeOrUnsupportedUrls()
{
    QFETCH(QString, url);
    QVERIFY2(!TikTokUrlValidator::isValid(url), qPrintable(url));
}

QTEST_APPLESS_MAIN(TestTikTokUrlValidator)

#include "TestTikTokUrlValidator.moc"
