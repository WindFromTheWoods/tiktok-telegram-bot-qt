#include "app/DiagnosticsService.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QUrlQuery>
#include <memory>

DiagnosticsService::DiagnosticsService(QObject* parent)
    : QObject(parent)
{
}
QVariantList DiagnosticsService::results() const
{
    return m_results;
}
void DiagnosticsService::put(const QString& name, const QString& state, const QString& detail)
{
    const QVariantMap row{{QStringLiteral("name"), name},
                          {QStringLiteral("state"), state},
                          {QStringLiteral("detail"), detail}};
    for (QVariant& value : m_results)
        if (value.toMap().value(QStringLiteral("name")).toString() == name)
        {
            value = row;
            emit changed();
            return;
        }
    m_results.append(row);
    emit changed();
}
void DiagnosticsService::checkTool(const QString& name, const QString& executable,
                                   const QStringList& arguments, const int generation)
{
    const QString path = QStandardPaths::findExecutable(executable);
    if (path.isEmpty())
    {
        put(name, QStringLiteral("error"), tr("Executable not found."));
        return;
    }
    put(name, QStringLiteral("checking"), tr("Checking version…"));
    auto* process = new QProcess(this);
    auto* timeout = new QTimer(process);
    timeout->setSingleShot(true);
    auto output = std::make_shared<QByteArray>();
    connect(process, &QProcess::readyReadStandardOutput, process,
            [process, output]
            {
                output->append(process->readAllStandardOutput());
                if (output->size() > 2048)
                    *output = output->right(2048);
            });
    connect(process, &QProcess::readyReadStandardError, process,
            [process] { process->readAllStandardError(); });
    connect(timeout, &QTimer::timeout, process,
            [process]
            {
                process->setProperty("timedOut", true);
                process->kill();
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process, name, generation](QProcess::ProcessError error)
            {
                if (error == QProcess::FailedToStart)
                {
                    if (generation == m_generation)
                        put(name, QStringLiteral("error"), tr("Could not start the executable."));
                    process->deleteLater();
                }
            });
    connect(process, &QProcess::finished, this,
            [this, process, name, output, generation](int code, QProcess::ExitStatus status)
            {
                if (generation == m_generation)
                {
                    const bool ok = status == QProcess::NormalExit && code == 0;
                    put(name, ok ? QStringLiteral("ok") : QStringLiteral("error"),
                        process->property("timedOut").toBool() ? tr("Version check timed out.")
                        : ok ? QString::fromUtf8(*output).section('\n', 0, 0).left(180)
                             : tr("Version check failed."));
                }
                process->deleteLater();
            });
    process->start(path, arguments);
    timeout->start(10000);
}
void DiagnosticsService::run(const QString& downloader, const QString& library,
                             const QString& token, const QList<ChannelRecord>& channels,
                             const bool userPublisher)
{
    ++m_generation;
    const int generation = m_generation;
    for (auto* reply : m_network.findChildren<QNetworkReply*>())
        reply->abort();
    for (auto* process : findChildren<QProcess*>())
        process->kill();
    m_results.clear();
    checkTool(QStringLiteral("yt-dlp"), downloader,
              {QStringLiteral("--ignore-config"), QStringLiteral("--version")}, generation);
    checkTool(QStringLiteral("FFmpeg"), QStringLiteral("ffmpeg"), {QStringLiteral("-version")},
              generation);
    if (userPublisher)
    {
        const QString path = library.isEmpty() ? QDir(QCoreApplication::applicationDirPath())
                                                     .filePath(QStringLiteral("tdjson.dll"))
                                               : library;
        put(QStringLiteral("TDLib"),
            QFileInfo(path).isFile() ? QStringLiteral("ok") : QStringLiteral("error"),
            QFileInfo(path).isFile() ? tr("Library exists; connection validates compatibility.")
                                     : tr("Select an existing TDLib library."));
        put(tr("User account permissions"), QStringLiteral("info"),
            tr("Bot permissions below are separate from the TDLib user account's permissions."));
    }
    if (token.trimmed().isEmpty())
    {
        put(QStringLiteral("Telegram"), QStringLiteral("error"), tr("Enter a bot token first."));
        return;
    }
    put(QStringLiteral("Telegram"), QStringLiteral("checking"), tr("Checking bot identity…"));
    const QString base = QStringLiteral("https://api.telegram.org/bot%1/").arg(token);
    QNetworkRequest request(QUrl(base + QStringLiteral("getMe")));
    request.setTransferTimeout(10000);
    auto* reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, base, channels, generation]
            {
                const auto payload = QJsonDocument::fromJson(reply->readAll()).object();
                const bool ok = reply->error() == QNetworkReply::NoError &&
                                payload.value(QStringLiteral("ok")).toBool();
                reply->deleteLater();
                if (generation != m_generation)
                    return;
                put(QStringLiteral("Telegram"), ok ? QStringLiteral("ok") : QStringLiteral("error"),
                    ok ? tr("Bot token accepted.")
                       : tr("Bot identity check failed. Check token and connection."));
                if (!ok)
                    return;
                const qint64 userId = payload.value(QStringLiteral("result"))
                                          .toObject()
                                          .value(QStringLiteral("id"))
                                          .toInteger();
                for (const auto& channel : channels)
                {
                    const QString name = tr("Channel: %1").arg(channel.name);
                    put(name, QStringLiteral("checking"), tr("Checking publication permission…"));
                    QUrl url(base + QStringLiteral("getChatMember"));
                    QUrlQuery query;
                    query.addQueryItem(QStringLiteral("chat_id"), channel.telegramId);
                    query.addQueryItem(QStringLiteral("user_id"), QString::number(userId));
                    url.setQuery(query);
                    QNetworkRequest permissionRequest(url);
                    permissionRequest.setTransferTimeout(10000);
                    auto* permission = m_network.get(permissionRequest);
                    connect(
                        permission, &QNetworkReply::finished, this,
                        [this, permission, name, generation]
                        {
                            const auto object =
                                QJsonDocument::fromJson(permission->readAll()).object();
                            const auto member = object.value(QStringLiteral("result")).toObject();
                            const QString status =
                                member.value(QStringLiteral("status")).toString();
                            const bool allowed =
                                permission->error() == QNetworkReply::NoError &&
                                object.value(QStringLiteral("ok")).toBool() &&
                                (status == QStringLiteral("creator") ||
                                 (status == QStringLiteral("administrator") &&
                                  member.value(QStringLiteral("can_post_messages")).toBool()));
                            permission->deleteLater();
                            if (generation == m_generation)
                                put(name, allowed ? QStringLiteral("ok") : QStringLiteral("error"),
                                    allowed ? tr("Bot can publish to this channel.")
                                            : tr("Channel unavailable or bot lacks permission to "
                                                 "publish."));
                        });
                }
            });
}
