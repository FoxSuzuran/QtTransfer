#include <QCoreApplication>
#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QNetworkProxy>
#include <QTcpServer>
#include <QTcpSocket>
#include <cstdlib>

class Receiver : public QObject
{
    Q_OBJECT

public:
    Receiver(quint16 port, const QString &saveDir, QObject *parent = nullptr)
        : QObject(parent), saveDir(saveDir)
    {
        QDir().mkpath(saveDir);
        server.setProxy(QNetworkProxy::NoProxy);

        connect(&server, &QTcpServer::newConnection, this, &Receiver::acceptConnection);

        if (!server.listen(QHostAddress::AnyIPv4, port)) {
            qInfo() << "listen failed:" << server.errorString();
            std::exit(1);
        }

        qInfo() << "Listening on port" << port;
        qInfo() << "Save directory:" << QDir(saveDir).absolutePath();
    }

private slots:
    void acceptConnection()
    {
        socket = server.nextPendingConnection();
        file.close();
        fileName.clear();
        headerSize = 0;
        fileSize = 0;
        received = 0;

        connect(socket, &QTcpSocket::readyRead, this, &Receiver::readData);
        connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
        connect(socket, &QTcpSocket::disconnected, this, &Receiver::clientDisconnected);

        qInfo() << "Client connected:" << socket->peerAddress().toString();
        qInfo() << "Current save directory:" << QDir(saveDir).absolutePath();
    }

    void readData()
    {
        if (!socket) {
            return;
        }

        QDataStream in(socket);
        in.setVersion(QDataStream::Qt_5_0);

        if (headerSize == 0) {
            if (socket->bytesAvailable() < static_cast<qint64>(sizeof(quint32))) {
                return;
            }

            in >> headerSize;
        }

        if (fileName.isEmpty()) {
            if (socket->bytesAvailable() < headerSize) {
                return;
            }

            in >> fileName >> fileSize;
            file.setFileName(QDir(saveDir).filePath(fileName));

            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                qInfo() << "Open file failed:" << file.fileName();
                socket->disconnectFromHost();
                return;
            }

            qInfo() << "Receiving file:" << fileName;
            qInfo() << "File size:" << fileSize;
        }

        QByteArray data = socket->readAll();
        if (!data.isEmpty()) {
            file.write(data);
            received += data.size();
        }

        if (fileName.isEmpty()) {
            return;
        }

        if (received >= fileSize) {
            file.close();
            qInfo() << "Saved file:" << file.fileName();
            socket->write("OK");
            socket->waitForBytesWritten(3000);
            socket->disconnectFromHost();
        }
    }

private:
    void clientDisconnected()
    {
        qInfo() << "Client disconnected";
        if (file.isOpen()) {
            file.close();
        }
        if (socket) {
            socket = nullptr;
        }
    }

    QTcpServer server;
    QTcpSocket *socket = nullptr;
    QFile file;
    QString saveDir;
    QString fileName;
    quint32 headerSize = 0;
    qint64 fileSize = 0;
    qint64 received = 0;
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
