#include <QCoreApplication>
#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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
        : QObject(parent), saveDir(QDir(saveDir).absolutePath())
    {
        QDir().mkpath(this->saveDir);
        server.setProxy(QNetworkProxy::NoProxy);

        connect(&server, &QTcpServer::newConnection, this, &Receiver::acceptConnection);

        if (!server.listen(QHostAddress::AnyIPv4, port)) {
            qInfo() << "listen failed:" << server.errorString();
            std::exit(1);
        }

        qInfo() << "Listening on port" << port;
        qInfo() << "Start directory:" << this->saveDir;
    }

private slots:
    void acceptConnection()
    {
        socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);

        qInfo() << "Client connected:" << socket->peerAddress().toString();
        qInfo() << "Current directory:" << saveDir;

        while (socket->bytesAvailable() < static_cast<qint64>(sizeof(quint32))) {
            if (!socket->waitForReadyRead(5000)) {
                socket->disconnectFromHost();
                socket = nullptr;
                return;
            }
        }

        quint32 headerSize = 0;
        QDataStream in(socket);
        in.setVersion(QDataStream::Qt_5_0);
        in >> headerSize;

        while (socket->bytesAvailable() < headerSize) {
            if (!socket->waitForReadyRead(5000)) {
                socket->disconnectFromHost();
                socket = nullptr;
                return;
            }
        }

        QByteArray header = socket->read(headerSize);
        QDataStream headerStream(&header, QIODevice::ReadOnly);
        headerStream.setVersion(QDataStream::Qt_5_0);

        QString command;
        headerStream >> command;

        if (command == "PUT") {
            QString fileName;
            qint64 fileSize = 0;
            headerStream >> fileName >> fileSize;

            if (!QFileInfo(fileName).isAbsolute()) {
                fileName = QDir(saveDir).filePath(fileName);
            }

            QFileInfo fileInfo(fileName);
            QDir().mkpath(fileInfo.path());

            QFile file(fileInfo.filePath());
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                qInfo() << "Open file failed:" << file.fileName();
                socket->disconnectFromHost();
                socket = nullptr;
                return;
            }

            qInfo() << "Receiving file:" << fileName;
            qInfo() << "File size:" << fileSize;

            qint64 received = 0;
            while (received < fileSize) {
                if (socket->bytesAvailable() == 0 && !socket->waitForReadyRead(5000)) {
                    file.close();
                    socket->disconnectFromHost();
                    socket = nullptr;
                    return;
                }

                QByteArray data = socket->read(qMin<qint64>(64 * 1024, fileSize - received));
                if (!data.isEmpty()) {
                    file.write(data);
                    received += data.size();
                }
            }

            file.close();
            qInfo() << "Saved file:" << file.fileName();
            socket->write("OK");
            socket->waitForBytesWritten(3000);
        } else if (command == "MKDIR") {
            QString dirPath;
            headerStream >> dirPath;

            if (!QFileInfo(dirPath).isAbsolute()) {
                dirPath = QDir(saveDir).filePath(dirPath);
            }

            QDir().mkpath(dirPath);
            qInfo() << "Created directory:" << dirPath;
            socket->write("OK");
            socket->waitForBytesWritten(3000);
        } else if (command == "LIST") {
            QString dirPath;
            headerStream >> dirPath;

            if (dirPath.isEmpty()) {
                dirPath = saveDir;
            } else if (!QFileInfo(dirPath).isAbsolute()) {
                dirPath = QDir(saveDir).filePath(dirPath);
            }

            QDir dir(dirPath);
            if (!dir.exists()) {
                dir.setPath(saveDir);
            }

            QString currentPath = dir.absolutePath();
            QStringList files;

            if (currentPath != dir.rootPath()) {
                files << "../";
            }

            QFileInfoList fileInfos = dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name);
            for (const QFileInfo &fileInfo : fileInfos) {
                files << (fileInfo.isDir() ? fileInfo.fileName() + "/" : fileInfo.fileName());
            }

            QByteArray reply;
            QDataStream replyStream(&reply, QIODevice::WriteOnly);
            replyStream.setVersion(QDataStream::Qt_5_0);
            replyStream << currentPath << files;

            QDataStream out(socket);
            out.setVersion(QDataStream::Qt_5_0);
            out << static_cast<quint32>(reply.size());
            socket->write(reply);
            socket->waitForBytesWritten(3000);
        } else if (command == "GET") {
            QString fileName;
            headerStream >> fileName;

            if (!QFileInfo(fileName).isAbsolute()) {
                fileName = QDir(saveDir).filePath(fileName);
            }

            QFile file(fileName);
            QByteArray reply;
            QDataStream replyStream(&reply, QIODevice::WriteOnly);
            replyStream.setVersion(QDataStream::Qt_5_0);

            if (!file.open(QIODevice::ReadOnly)) {
                replyStream << static_cast<qint64>(-1);
                QDataStream out(socket);
                out.setVersion(QDataStream::Qt_5_0);
                out << static_cast<quint32>(reply.size());
                socket->write(reply);
                socket->waitForBytesWritten(3000);
            } else {
                replyStream << static_cast<qint64>(file.size());
                QDataStream out(socket);
                out.setVersion(QDataStream::Qt_5_0);
                out << static_cast<quint32>(reply.size());
                socket->write(reply);
                socket->waitForBytesWritten(3000);

                while (!file.atEnd()) {
                    socket->write(file.read(64 * 1024));
                    if (!socket->waitForBytesWritten(5000)) {
                        file.close();
                        socket->disconnectFromHost();
                        socket = nullptr;
                        return;
                    }
                }

                file.close();
                qInfo() << "Sent file:" << fileName;
            }
        }

        socket->disconnectFromHost();
        socket = nullptr;
    }

private:
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
