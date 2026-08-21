#include "Logging.h"
#include "ui/SettingsController.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTimer>
#include <QVariant>

int main(int argc, char *argv[])
{
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QGuiApplication application(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("TikTokTelegramBot"));
    QGuiApplication::setApplicationName(QStringLiteral("TikTokTelegramBot"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("TikTok Telegram Bot"));
    QGuiApplication::setApplicationVersion(QStringLiteral("1.1.0"));

    SettingsController settingsController;
    QQmlApplicationEngine engine;
    engine.setInitialProperties(
        {{QStringLiteral("backend"),
          QVariant::fromValue(static_cast<QObject *>(&settingsController))}});
    engine.loadFromModule(QStringLiteral("TikTokBot.Ui"), QStringLiteral("Main"));

    if (engine.rootObjects().isEmpty()) {
        qCCritical(logApp) << "Could not create the QML user interface";
        return 3;
    }

    QObject::connect(&application, &QCoreApplication::aboutToQuit, &settingsController,
                     &SettingsController::stopBot);
    QTimer::singleShot(0, &settingsController, &SettingsController::startIfConfigured);

    return application.exec();
}
