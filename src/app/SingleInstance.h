#ifndef TIKTOK_BOT_SINGLE_INSTANCE_H
#define TIKTOK_BOT_SINGLE_INSTANCE_H

#include <QLocalServer>
#include <QLockFile>
#include <QObject>
#include <memory>

/** @brief Holds a per-data-directory lock and forwards repeated launches to the window. */
class SingleInstance final : public QObject
{
    Q_OBJECT
public:
    /** @brief Creates a lock owner; acquire() must precede database access. */
    explicit SingleInstance(const QString& dataDirectory, QObject* parent = nullptr);
    /** @brief Releases the owned local server; lock lifetime matches the instance. */
    ~SingleInstance() override;
    /** @return True only for the process that owns this data directory. */
    bool acquire();
    /** @brief Asynchronously asks the owner to show its existing window. */
    void activateExisting();
    /** @return Explanation when acquire() failed for a reason other than another owner. */
    QString error() const;
signals:
    /** @brief A repeated launch requested activation of the primary window. */
    void activationRequested();
    /** @brief Forwarding has finished or timed out; the secondary process may exit. */
    void forwardingFinished();

private:
    QString m_directory;
    QString m_serverName;
    QString m_error;
    std::unique_ptr<QLockFile> m_lock;
    QLocalServer m_server;
};

#endif
