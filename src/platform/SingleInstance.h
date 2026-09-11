#pragma once

#include <QLocalServer>
#include <QObject>
#include <QString>

class QLocalSocket;

namespace clipvault::platform {

// Keeps one owning process and delivers commands to it
class SingleInstance : public QObject {
    Q_OBJECT
public:
    explicit SingleInstance(QString key, QObject* parent = nullptr);
    ~SingleInstance() override;

    // False when another instance already holds the socket
    bool tryBecomePrimary();

    // False when nothing is listening or the write timed out
    bool sendToPrimary(const QString& command, int timeoutMs = 1500);

signals:
    void commandReceived(const QString& command);

private:
    void onNewConnection();
    void onPeerReadable(QLocalSocket* socket);

    QString m_key;
    QLocalServer m_server;
};

} // namespace clipvault::platform
