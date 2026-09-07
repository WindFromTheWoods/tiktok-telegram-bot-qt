/******************************************************************************
 * @file    TestAppConfig.cpp
 * @brief   Tests runtime configuration validation and defaults.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#include "Logging.h"
#include "config/AppConfig.h"

#include <QHash>
#include <QScopeGuard>
#include <QSet>
#include <QTest>

class TestAppConfig final : public QObject
{
    Q_OBJECT

private slots:
    void redactsCredentialsFromLogs()
    {
        const QString token = QStringLiteral("123456789:") + QString(35, QLatin1Char('A'));
        const QString apiHash = QStringLiteral("0123456789abcdef0123456789abcdef");
        const QString message =
            QStringLiteral("GET https://api.telegram.org/bot%1/getMe api=%2 keep=abc")
                .arg(token, apiHash);
        const QString redacted = redactSensitiveLogText(message, {apiHash, QStringLiteral("abc")});
        QVERIFY(!redacted.contains(token));
        QVERIFY(!redacted.contains(apiHash));
        QVERIFY(redacted.contains(QStringLiteral("[redacted]")));
        QVERIFY(redacted.contains(QStringLiteral("keep=abc")));
    }

    void acceptsUiValues();
    void appliesOptionalDefaults();
    void trimsValuesAndAcceptsIntervalBoundaries();
    void readsLegacyEnvironment();
    void rejectsInvalidValues_data();
    void rejectsInvalidValues();
};

void TestAppConfig::acceptsUiValues()
{
    QString error;
    const std::optional<AppConfig> config =
        AppConfig::fromValues(QStringLiteral("test-token"), QStringLiteral("-1001234567890"),
                              QStringLiteral("1839013137"), QStringLiteral("C:/Tools/yt-dlp.exe"),
                              QStringLiteral("120"), &error);

    QVERIFY2(config.has_value(), qPrintable(error));
    QCOMPARE(config->botToken(), QStringLiteral("test-token"));
    QCOMPARE(config->channelId(), QStringLiteral("-1001234567890"));
    QCOMPARE(config->adminUserId(), 1839013137LL);
    QCOMPARE(config->ytDlpExecutable(), QStringLiteral("C:/Tools/yt-dlp.exe"));
    QCOMPARE(config->publicationInterval().count(), 120LL);
}

void TestAppConfig::trimsValuesAndAcceptsIntervalBoundaries()
{
    QString error;
    const std::optional<AppConfig> minimum = AppConfig::fromValues(
        QStringLiteral(" token "), QStringLiteral(" @channel_name "), QStringLiteral(" 42 "),
        QStringLiteral(" C:/Tools/yt-dlp.exe "), QStringLiteral(" 1 "), &error);
    QVERIFY2(minimum.has_value(), qPrintable(error));
    QCOMPARE(minimum->botToken(), QStringLiteral("token"));
    QCOMPARE(minimum->channelId(), QStringLiteral("@channel_name"));
    QCOMPARE(minimum->ytDlpExecutable(), QStringLiteral("C:/Tools/yt-dlp.exe"));
    QCOMPARE(minimum->publicationInterval().count(), 1LL);

    const std::optional<AppConfig> maximum = AppConfig::fromValues(
        QStringLiteral("token"), QStringLiteral("@channel_name"), QStringLiteral("42"),
        QStringLiteral("yt-dlp"), QStringLiteral("10080"), &error);
    QVERIFY2(maximum.has_value(), qPrintable(error));
    QCOMPARE(maximum->publicationInterval().count(), 10080LL);
}

void TestAppConfig::readsLegacyEnvironment()
{
    static constexpr const char* Names[]{"TELEGRAM_BOT_TOKEN", "TELEGRAM_CHANNEL_ID",
                                         "TELEGRAM_ADMIN_USER_ID", "YTDLP_PATH",
                                         "TELEGRAM_POST_INTERVAL_MINUTES"};
    QHash<QByteArray, QByteArray> previousValues;
    QSet<QByteArray> previouslySet;
    for (const char* name : Names)
    {
        const QByteArray key(name);
        if (qEnvironmentVariableIsSet(name))
        {
            previouslySet.insert(key);
            previousValues.insert(key, qgetenv(name));
        }
    }
    const auto restoreEnvironment = qScopeGuard(
        [&previousValues, &previouslySet]()
        {
            for (const char* name : Names)
            {
                const QByteArray key(name);
                if (previouslySet.contains(key))
                {
                    qputenv(name, previousValues.value(key));
                }
                else
                {
                    qunsetenv(name);
                }
            }
        });

    qputenv("TELEGRAM_BOT_TOKEN", "environment-token");
    qputenv("TELEGRAM_CHANNEL_ID", "@environment_channel");
    qputenv("TELEGRAM_ADMIN_USER_ID", "987654321");
    qputenv("YTDLP_PATH", "C:/Tools/environment-yt-dlp.exe");
    qputenv("TELEGRAM_POST_INTERVAL_MINUTES", "240");

    QString error;
    const std::optional<AppConfig> config = AppConfig::fromEnvironment(&error);
    QVERIFY2(config.has_value(), qPrintable(error));
    QCOMPARE(config->botToken(), QStringLiteral("environment-token"));
    QCOMPARE(config->channelId(), QStringLiteral("@environment_channel"));
    QCOMPARE(config->adminUserId(), 987654321LL);
    QCOMPARE(config->publicationInterval().count(), 240LL);
}

void TestAppConfig::appliesOptionalDefaults()
{
    QString error;
    const std::optional<AppConfig> config =
        AppConfig::fromValues(QStringLiteral("test-token"), QStringLiteral("@channel_name"),
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
    QTest::newRow("channel username too short")
        << QStringLiteral("token") << QStringLiteral("@abcd") << QStringLiteral("42")
        << QStringLiteral("120") << QStringLiteral("TELEGRAM_CHANNEL_ID");
    QTest::newRow("channel username starts with digit")
        << QStringLiteral("token") << QStringLiteral("@1channel") << QStringLiteral("42")
        << QStringLiteral("120") << QStringLiteral("TELEGRAM_CHANNEL_ID");
    QTest::newRow("invalid administrator")
        << QStringLiteral("token") << QStringLiteral("@channel_name")
        << QStringLiteral("not-a-number") << QStringLiteral("120")
        << QStringLiteral("TELEGRAM_ADMIN_USER_ID");
    QTest::newRow("zero administrator")
        << QStringLiteral("token") << QStringLiteral("@channel_name") << QStringLiteral("0")
        << QStringLiteral("120") << QStringLiteral("TELEGRAM_ADMIN_USER_ID");
    QTest::newRow("negative administrator")
        << QStringLiteral("token") << QStringLiteral("@channel_name") << QStringLiteral("-42")
        << QStringLiteral("120") << QStringLiteral("TELEGRAM_ADMIN_USER_ID");
    QTest::newRow("interval below range")
        << QStringLiteral("token") << QStringLiteral("@channel_name") << QStringLiteral("42")
        << QStringLiteral("0") << QStringLiteral("TELEGRAM_POST_INTERVAL_MINUTES");
    QTest::newRow("interval above range")
        << QStringLiteral("token") << QStringLiteral("@channel_name") << QStringLiteral("42")
        << QStringLiteral("10081") << QStringLiteral("TELEGRAM_POST_INTERVAL_MINUTES");
    QTest::newRow("non-integer interval")
        << QStringLiteral("token") << QStringLiteral("@channel_name") << QStringLiteral("42")
        << QStringLiteral("2.5") << QStringLiteral("TELEGRAM_POST_INTERVAL_MINUTES");
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
