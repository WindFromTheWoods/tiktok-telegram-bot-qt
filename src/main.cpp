/******************************************************************************
 * @file    main.cpp
 * @brief   Initializes the Qt application, QML interface, and system tray.
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
#include "app/SingleInstance.h"
#include "ui/SettingsController.h"
#include "ui/TrayController.h"

#include <QApplication>
#include <QLocale>
#include <QMessageBox>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <QVariant>
#include <QWindow>

int main(int argc, char* argv[])
{
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QApplication application(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("TikTokTelegramBot"));
    QApplication::setApplicationName(QStringLiteral("TikTokTelegramBot"));
    QApplication::setApplicationDisplayName(QStringLiteral("TikTok Telegram Bot"));
    QApplication::setApplicationVersion(QStringLiteral("2.1.0"));
    application.setQuitOnLastWindowClosed(false);
    const bool uiSmokeTest = application.arguments().contains(QStringLiteral("--ui-smoke-test"));
    const auto option = [&application](const QString& prefix)
    {
        for (const QString& argument : application.arguments())
            if (argument.startsWith(prefix))
                return argument.mid(prefix.size());
        return QString{};
    };
    const QString screenshotPath =
        uiSmokeTest ? option(QStringLiteral("--ui-screenshot=")) : QString{};

    // Verification never reads live credentials, edits the real queue, or sends requests.
    QTemporaryDir testDirectory;
    if (uiSmokeTest)
    {
        if (!testDirectory.isValid())
            return 6;
        QStandardPaths::setTestModeEnabled(true);
        QApplication::setApplicationName(
            QStringLiteral("TikTokTelegramBot-UI-Test-%1").arg(QCoreApplication::applicationPid()));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, testDirectory.path());
    }
    const QString testDatabasePath = testDirectory.filePath(QStringLiteral("tiktok-bot.sqlite"));
    SingleInstance instance(
        uiSmokeTest ? testDirectory.path()
                    : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
    if (!instance.acquire())
    {
        if (!instance.error().isEmpty())
        {
            QMessageBox::critical(nullptr, QStringLiteral("TikTok Telegram Bot"), instance.error());
            return 5;
        }
        QObject::connect(&instance, &SingleInstance::forwardingFinished, &application,
                         &QCoreApplication::quit);
        instance.activateExisting();
        return application.exec();
    }

    QTranslator translator;

    if (uiSmokeTest && application.arguments().contains(QStringLiteral("--ui-demo")))
    {
        AppDatabase fixture(testDatabasePath);
        if (!fixture.open())
            return 6;
        const auto channel = fixture.ensureDefaultChannel(QStringLiteral("@demo_channel"));
        static_cast<void>(fixture.setChannelTimeZone(channel, QStringLiteral("Europe/Kyiv")));
        static_cast<void>(
            fixture.setChannelCaptionTemplate(channel, QStringLiteral("{title} — @{author}")));
        static_cast<void>(fixture.addScheduleSlot(channel, 0, QStringLiteral("18:30")));
        for (int i = 0; i < 4; ++i)
        {
            const auto id = fixture.addVideo(
                QStringLiteral("https://www.tiktok.com/@demo/video/123456789%1").arg(i), 0, 0,
                channel, QDateTime::currentDateTimeUtc().addSecs(3600 + i * 7200), nullptr, i != 0);
            static_cast<void>(fixture.updateMetadata(
                id,
                QStringList{QStringLiteral("A quiet morning in the mountains"),
                            QStringLiteral("Behind the scenes: a new idea"),
                            QStringLiteral("A short guide to better videos"),
                            QStringLiteral("Sunset over the city")}
                    .at(i),
                QStringLiteral("demo_creator"), 34 + i, {}));
            static_cast<void>(fixture.markReady(id, QStringLiteral("demo.mp4"), 1024 * 1024));
            if (i == 3)
            {
                static_cast<void>(fixture.markUploading(id));
                static_cast<void>(fixture.markDeliveryUnknown(
                    id, QStringLiteral("Connection ended before delivery was confirmed.")));
            }
        }
    }

    SettingsController settingsController(nullptr, uiSmokeTest ? testDatabasePath : QString{});
    QQmlApplicationEngine engine;
    const auto loadLanguage = [&]
    {
        application.removeTranslator(&translator);
        const QString selected = settingsController.uiLanguage();
        const QString language =
            selected == QStringLiteral("system") ? QLocale::system().name() : selected;
        if (language.startsWith(QStringLiteral("ru")) &&
            translator.load(QStringLiteral(":/i18n/TikTokTelegramBot_ru_RU.qm")))
            application.installTranslator(&translator);
        engine.retranslate();
    };
    loadLanguage();
    QObject::connect(&settingsController, &SettingsController::languageChanged, &engine,
                     loadLanguage);
    engine.setInitialProperties(
        {{QStringLiteral("backend"),
          QVariant::fromValue(static_cast<QObject*>(&settingsController))}});
    if (uiSmokeTest)
        engine.setInitialProperties(
            {{QStringLiteral("backend"),
              QVariant::fromValue(static_cast<QObject*>(&settingsController))},
             {QStringLiteral("initialPage"), option(QStringLiteral("--ui-page=")).toInt()},
             {QStringLiteral("testDialog"), option(QStringLiteral("--ui-dialog="))}});
    engine.loadFromModule(QStringLiteral("TikTokBot.Ui"), QStringLiteral("Main"));

    if (engine.rootObjects().isEmpty())
    {
        qCCritical(logApp) << "Could not create the QML user interface";
        return 3;
    }

    auto* window = qobject_cast<QWindow*>(engine.rootObjects().constFirst());
    if (window == nullptr)
    {
        qCCritical(logApp) << "QML root object is not a window";
        return 4;
    }
    if (uiSmokeTest && screenshotPath.isEmpty())
    {
        window->hide();
    }
    TrayController trayController(window);
    QObject::connect(&instance, &SingleInstance::activationRequested, &trayController,
                     &TrayController::showWindow);
    QObject::connect(&trayController, &TrayController::startRequested, &settingsController,
                     &SettingsController::saveAndStart);
    QObject::connect(&trayController, &TrayController::stopRequested, &settingsController,
                     &SettingsController::stopBot);
    QObject::connect(&trayController, &TrayController::quitRequested, &application,
                     &QCoreApplication::quit);
    QObject::connect(&settingsController, &SettingsController::botRunningChanged, &trayController,
                     [&settingsController, &trayController]
                     { trayController.setBotRunning(settingsController.botRunning()); });
    QObject::connect(&settingsController, &SettingsController::notificationRequested,
                     &trayController, &TrayController::showNotification);
    QObject::connect(&settingsController, &SettingsController::applicationQuitRequested,
                     &application, &QCoreApplication::quit);

    QObject::connect(&application, &QCoreApplication::aboutToQuit, &settingsController,
                     &SettingsController::stopBot);
    if (uiSmokeTest)
    {
        QTimer::singleShot(1500, &application,
                           [&application, window, screenshotPath]
                           {
                               if (screenshotPath.isEmpty())
                               {
                                   application.quit();
                                   return;
                               }
                               auto* quickWindow = qobject_cast<QQuickWindow*>(window);
                               if (quickWindow == nullptr || quickWindow->contentItem() == nullptr)
                               {
                                   application.exit(7);
                                   return;
                               }
                               const QSharedPointer<QQuickItemGrabResult> result =
                                   quickWindow->contentItem()->grabToImage();
                               if (!result)
                               {
                                   application.exit(7);
                                   return;
                               }
                               QObject::connect(result.data(), &QQuickItemGrabResult::ready,
                                                &application,
                                                [&application, screenshotPath, result]
                                                {
                                                    if (!result->saveToFile(screenshotPath))
                                                        application.exit(7);
                                                    else
                                                        application.quit();
                                                });
                           });
        QTimer::singleShot(7000, &application, [&application] { application.exit(8); });
    }
    else
    {
        QTimer::singleShot(0, &settingsController, &SettingsController::startIfConfigured);
        if (application.arguments().contains(QStringLiteral("--minimized")))
        {
            QTimer::singleShot(0, &trayController, &TrayController::hideWindow);
        }
    }

    return application.exec();
}
