#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

class Receiver : public QObject
{
    Q_OBJECT

public:
    Receiver(quint16 port, const QString &saveDir, QObject *parent = nullptr)
        : QObject(parent), saveDir(saveDir)
    {
        QDir().mkpath(saveDir);

        connect(&server, &QTcpServer::newConnection, this, &Receiver::acceptConnection);

        if (!server.listen(QHostAddress::Any, port)) {
            qFatal("listen failed");
        }

        qInfo() << "Listening on port" << port;
        qInfo() << "Save directory:" << QDir(saveDir).absolutePath();
    }

private slots:
    void acceptConnection()
    {
        socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
        connect(socket, &QTcpSocket::disconnected, this, &Receiver::clientDisconnected);

        qInfo() << "Client connected:" << socket->peerAddress().toString();
        qInfo() << "Current save directory:" << QDir(saveDir).absolutePath();
        socket->disconnectFromHost();
    }

private:
    void clientDisconnected()
    {
        qInfo() << "Client disconnected";
        if (socket) {
            socket = nullptr;
        }
    }

    QTcpServer server;
    QTcpSocket *socket = nullptr;
    QString saveDir;
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    quint16 port = 8899;
    QString saveDir = "received";
    QStringList args = app.arguments();

    if (args.size() > 1) {
        port = args.at(1).toUShort();
    }

    if (args.size() > 2) {
        saveDir = args.at(2);
    }

    Receiver receiver(port, saveDir);
    return app.exec();
}

#include "main.moc"
