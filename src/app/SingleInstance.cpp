#include "app/SingleInstance.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QLocalSocket>
#include <QTimer>

SingleInstance::SingleInstance(const QString& dataDirectory, QObject* parent)
    : QObject(parent)
    , m_directory(dataDirectory)
{
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&m_server, &QLocalServer::newConnection, this,
            [this]
            {
                while (QLocalSocket* socket = m_server.nextPendingConnection())
                {
                    const auto processActivation = [this, socket]
                    {
                        QByteArray message = socket->property("activationMessage").toByteArray();
                        message += socket->readAll();
                        socket->setProperty("activationMessage", message.left(64));
                        if (message == QByteArrayLiteral("activate"))
                        {
                            emit activationRequested();
                            socket->disconnectFromServer();
                        }
                        else if (message.size() >= 64)
                        {
                            socket->abort();
                        }
                    };
                    connect(socket, &QLocalSocket::readyRead, socket, processActivation);
                    connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
                    QTimer::singleShot(2000, socket, &QObject::deleteLater);
                    if (socket->bytesAvailable() > 0)
                        QTimer::singleShot(0, socket, processActivation);
                }
            });
}

SingleInstance::~SingleInstance()
{
    m_server.close();
}

bool SingleInstance::acquire()
{
    if (!QDir().mkpath(m_directory))
    {
        m_error = tr("Cannot create the application data directory.");
        return false;
    }
    QString identity = QFileInfo(m_directory).canonicalFilePath();
#ifdef Q_OS_WIN
    identity = identity.toLower();
#endif
    m_serverName =
        QStringLiteral("TikTokBot-") +
        QString::fromLatin1(QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256)
                                .toHex()
                                .left(24));
    m_lock =
        std::make_unique<QLockFile>(QDir(m_directory).filePath(QStringLiteral("application.lock")));
    m_lock->setStaleLockTime(0);
    if (!m_lock->tryLock())
    {
        if (m_lock->error() != QLockFile::LockFailedError)
            m_error = tr("Cannot lock the application data directory.");
        return false;
    }
    QLocalServer::removeServer(m_serverName);
    if (!m_server.listen(m_serverName))
    {
        m_error = tr("Cannot create the application activation endpoint.");
        m_lock->unlock();
        return false;
    }
    return true;
}

void SingleInstance::activateExisting()
{
    auto* socket = new QLocalSocket(this);
    connect(socket, &QLocalSocket::connected, this,
            [socket]
            {
                socket->write("activate");
                socket->flush();
                socket->disconnectFromServer();
            });
    connect(socket, &QLocalSocket::disconnected, this, &SingleInstance::forwardingFinished);
    QTimer::singleShot(1500, this, &SingleInstance::forwardingFinished);
    socket->connectToServer(m_serverName);
}

QString SingleInstance::error() const
{
    return m_error;
}
