#include "SingleInstance.h"

#include "../core/Logging.h"

#include <QLocalSocket>
#include <QTimer>

namespace clipvault::platform {
namespace {

// Commands are single words so anything larger is foreign
constexpr qint64 kMaxCommandBytes = 256;
constexpr int kPeerTimeoutMs = 2000;

} // namespace

SingleInstance::SingleInstance(QString key, QObject* parent)
    : QObject(parent)
    , m_key(std::move(key))
{
    connect(&m_server, &QLocalServer::newConnection, this, &SingleInstance::onNewConnection);
}

SingleInstance::~SingleInstance()
{
    if (m_server.isListening())
        m_server.close();
}

bool SingleInstance::tryBecomePrimary()
{
    // Only this user may drive the running instance
    m_server.setSocketOptions(QLocalServer::UserAccessOption);

    if (m_server.listen(m_key))
        return true;

    // A crashed process leaves a socket that also reports AddressInUse
    if (m_server.serverError() == QAbstractSocket::AddressInUseError) {
        QLocalSocket probe;
        probe.connectToServer(m_key);
        if (probe.waitForConnected(300)) {
            probe.disconnectFromServer();
            return false;
        }
        qCWarning(lcApp) << "removing a stale ClipVault socket";
        QLocalServer::removeServer(m_key);
        if (m_server.listen(m_key))
            return true;
    }

    qCCritical(lcApp) << "could not listen on the instance socket:" << m_server.errorString();
    return false;
}

bool SingleInstance::sendToPrimary(const QString& command, int timeoutMs)
{
    QLocalSocket socket;
    socket.connectToServer(m_key);
    if (!socket.waitForConnected(timeoutMs))
        return false;
    socket.write(command.toUtf8());
    socket.write("\n");
    if (!socket.waitForBytesWritten(timeoutMs))
        return false;
    socket.disconnectFromServer();
    return true;
}

void SingleInstance::onNewConnection()
{
    // Reads stay asynchronous so a silent peer cannot stall capture
    while (QLocalSocket* socket = m_server.nextPendingConnection()) {
        connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { onPeerReadable(socket); });

        auto* deadline = new QTimer(socket);
        deadline->setSingleShot(true);
        connect(deadline, &QTimer::timeout, socket, &QLocalSocket::disconnectFromServer);
        deadline->start(kPeerTimeoutMs);
    }
}

void SingleInstance::onPeerReadable(QLocalSocket* socket)
{
    if (socket->bytesAvailable() > kMaxCommandBytes) {
        qCWarning(lcApp) << "oversized instance command discarded";
        socket->disconnectFromServer();
        return;
    }
    if (!socket->canReadLine())
        return;

    const QString command = QString::fromUtf8(socket->readLine()).trimmed();
    socket->disconnectFromServer();

    if (command.isEmpty())
        return;
    qCDebug(lcApp) << "received instance command" << command;
    emit commandReceived(command);
}

} // namespace clipvault::platform
