#include "config/AppConfig.h"

#include <QTest>

class TestAppConfig final : public QObject
{
    Q_OBJECT

private slots:
    void acceptsUiValues();
    void appliesOptionalDefaults();
    void rejectsInvalidValues_data();
    void rejectsInvalidValues();
};

void TestAppConfig::acceptsUiValues()
{
    QString error;
    const std::optional<AppConfig> config = AppConfig::fromValues(
        QStringLiteral("test-token"), QStringLiteral("-1001234567890"),
        QStringLiteral("1839013137"), QStringLiteral("C:/Tools/yt-dlp.exe"),
        QStringLiteral("120"), &error);

    QVERIFY2(config.has_value(), qPrintable(error));
    QCOMPARE(config->botToken(), QStringLiteral("test-token"));
    QCOMPARE(config->channelId(), QStringLiteral("-1001234567890"));
    QCOMPARE(config->adminUserId(), 1839013137LL);
    QCOMPARE(config->ytDlpExecutable(), QStringLiteral("C:/Tools/yt-dlp.exe"));
    QCOMPARE(config->publicationInterval().count(), 120LL);
}

void TestAppConfig::appliesOptionalDefaults()
{
    QString error;
    const std::optional<AppConfig> config = AppConfig::fromValues(
        QStringLiteral("test-token"), QStringLiteral("@channel_name"),
        QStringLiteral("42"), {}, {}, &error);

    QVERIFY2(config.has_value(), qPrintable(error));
    QCOMPARE(config->ytDlpExecutable(), QStringLiteral("yt-dlp"));
    QCOMPARE(config->publicationInterval().count(), 120LL);
}

void TestAppConfig::rejectsInvalidValues_data()
{
    QTest::addColumn<QString>("token");
    QTest::addColumn<QString>("channelId");
    QTest::addColumn<QString>("adminUserId");
    QTest::addColumn<QString>("interval");
    QTest::addColumn<QString>("expectedError");

    QTest::newRow("empty token") << QString{} << QStringLiteral("-1001234567890")
                                  << QStringLiteral("42") << QStringLiteral("120")
                                  << QStringLiteral("TELEGRAM_BOT_TOKEN");
    QTest::newRow("positive numeric channel")
        << QStringLiteral("token") << QStringLiteral("123") << QStringLiteral("42")
        << QStringLiteral("120") << QStringLiteral("TELEGRAM_CHANNEL_ID");
    QTest::newRow("invalid administrator")
        << QStringLiteral("token") << QStringLiteral("@channel_name")
        << QStringLiteral("not-a-number") << QStringLiteral("120")
        << QStringLiteral("TELEGRAM_ADMIN_USER_ID");
    QTest::newRow("interval below range")
        << QStringLiteral("token") << QStringLiteral("@channel_name")
        << QStringLiteral("42") << QStringLiteral("0")
        << QStringLiteral("TELEGRAM_POST_INTERVAL_MINUTES");
    QTest::newRow("interval above range")
        << QStringLiteral("token") << QStringLiteral("@channel_name")
        << QStringLiteral("42") << QStringLiteral("10081")
        << QStringLiteral("TELEGRAM_POST_INTERVAL_MINUTES");
}

void TestAppConfig::rejectsInvalidValues()
{
    QFETCH(QString, token);
    QFETCH(QString, channelId);
    QFETCH(QString, adminUserId);
    QFETCH(QString, interval);
    QFETCH(QString, expectedError);

    QString error;
    const std::optional<AppConfig> config = AppConfig::fromValues(
        token, channelId, adminUserId, QStringLiteral("yt-dlp"), interval, &error);

    QVERIFY(!config.has_value());
    QVERIFY2(error.contains(expectedError), qPrintable(error));
}

QTEST_APPLESS_MAIN(TestAppConfig)

#include "TestAppConfig.moc"
