#include "mainwindow.h"

#include <QDataStream>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QNetworkProxy>
#include <QPushButton>
#include <QTcpSocket>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle("QtTransfer Sender");
    resize(620, 460);

    QLabel *ipLabel = new QLabel("接收端 IP:");
    QLabel *portLabel = new QLabel("端口:");
    QLabel *fileLabel = new QLabel("本地路径:");
    QLabel *remoteLabel = new QLabel("远端目录:");

    ipEdit = new QLineEdit("127.0.0.1");
    portEdit = new QLineEdit("8899");
    fileEdit = new QLineEdit;
    fileEdit->setReadOnly(true);
    remotePathLabel = new QLabel("未刷新");
    remotePathLabel->setWordWrap(true);
    remoteList = new QListWidget;

    QPushButton *browseFileButton = new QPushButton("选择文件");
    QPushButton *browseDirButton = new QPushButton("选择目录");
    QPushButton *sendButton = new QPushButton("发送选中");
    QPushButton *refreshButton = new QPushButton("刷新列表");
    QPushButton *downloadButton = new QPushButton("下载选中");

    statusLabel = new QLabel("请选择文件或目录");

    QHBoxLayout *ipLayout = new QHBoxLayout;
    ipLayout->addWidget(ipLabel);
    ipLayout->addWidget(ipEdit);
    ipLayout->addWidget(portLabel);
    ipLayout->addWidget(portEdit);

    QHBoxLayout *fileLayout = new QHBoxLayout;
    fileLayout->addWidget(fileLabel);
    fileLayout->addWidget(fileEdit);
    fileLayout->addWidget(browseFileButton);
    fileLayout->addWidget(browseDirButton);

    QHBoxLayout *remoteButtonLayout = new QHBoxLayout;
    remoteButtonLayout->addWidget(refreshButton);
    remoteButtonLayout->addWidget(downloadButton);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(ipLayout);
    mainLayout->addLayout(fileLayout);
    mainLayout->addWidget(remoteLabel);
    mainLayout->addWidget(remotePathLabel);
    mainLayout->addWidget(remoteList);
    mainLayout->addLayout(remoteButtonLayout);
    mainLayout->addWidget(statusLabel);
    mainLayout->addWidget(sendButton);

    setStyleSheet(
        "QWidget { background: #f6f7fb; font-size: 14px; }"
        "QLineEdit { background: white; border: 1px solid #cfd6e4; border-radius: 6px; padding: 6px; }"
        "QListWidget { background: white; border: 1px solid #cfd6e4; border-radius: 6px; }"
        "QPushButton { background: #2f6fed; color: white; border: none; border-radius: 6px; padding: 8px 14px; }"
        "QPushButton:hover { background: #1f5dd8; }"
        "QLabel { color: #243042; }");

    connect(browseFileButton, &QPushButton::clicked, this, &MainWindow::chooseFile);
    connect(browseDirButton, &QPushButton::clicked, this, &MainWindow::chooseDirectory);
    connect(sendButton, &QPushButton::clicked, this, &MainWindow::sendFile);
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::refreshFiles);
    connect(downloadButton, &QPushButton::clicked, this, &MainWindow::downloadFile);
    connect(remoteList, &QListWidget::itemDoubleClicked, this, &MainWindow::openRemoteItem);
}

void MainWindow::chooseFile()
{
    QString path = QFileDialog::getOpenFileName(this, "选择要发送的文件");

    if (!path.isEmpty()) {
        fileEdit->setText(path);
        statusLabel->setText("文件已选择，可以发送");
    }
}

void MainWindow::chooseDirectory()
{
    QString path = QFileDialog::getExistingDirectory(this, "选择要发送的目录");

    if (!path.isEmpty()) {
        fileEdit->setText(path);
        statusLabel->setText("目录已选择，可以发送");
    }
}

void MainWindow::sendFile()
{
    if (fileEdit->text().isEmpty()) {
        QMessageBox::warning(this, "提示", "请先选择文件或目录");
        return;
    }

    if (ipEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "提示", "请输入接收端 IP");
        return;
    }

    QFileInfo info(fileEdit->text());
    if (!info.exists()) {
        QMessageBox::warning(this, "提示", "本地路径不存在");
        return;
    }

    auto makeRemoteDir = [&](const QString &remoteDir) {
        QTcpSocket socket;
        socket.setProxy(QNetworkProxy::NoProxy);
        socket.connectToHost(ipEdit->text().trimmed(), portEdit->text().toUShort());

        if (!socket.waitForConnected(5000)) {
            return false;
        }

        QByteArray header;
        QDataStream headerStream(&header, QIODevice::WriteOnly);
        headerStream.setVersion(QDataStream::Qt_5_0);
        headerStream << QString("MKDIR") << remoteDir;

        QDataStream socketStream(&socket);
        socketStream.setVersion(QDataStream::Qt_5_0);
        socketStream << static_cast<quint32>(header.size());
        socket.write(header);
        socket.waitForBytesWritten(3000);

        if (!socket.waitForReadyRead(5000) || !socket.readAll().contains("OK")) {
            return false;
        }

        socket.disconnectFromHost();
        return true;
    };

    auto sendOneFile = [&](const QString &localPath, const QString &remotePath) {
        QFile file(localPath);
        if (!file.open(QIODevice::ReadOnly)) {
            return false;
        }

        QTcpSocket socket;
        socket.setProxy(QNetworkProxy::NoProxy);
        socket.connectToHost(ipEdit->text().trimmed(), portEdit->text().toUShort());

        if (!socket.waitForConnected(5000)) {
            return false;
        }

        QByteArray header;
        QDataStream headerStream(&header, QIODevice::WriteOnly);
        headerStream.setVersion(QDataStream::Qt_5_0);
        headerStream << QString("PUT") << remotePath << static_cast<qint64>(file.size());

        QDataStream socketStream(&socket);
        socketStream.setVersion(QDataStream::Qt_5_0);
        socketStream << static_cast<quint32>(header.size());
        socket.write(header);
        if (!socket.waitForBytesWritten(3000)) {
            return false;
        }

        while (!file.atEnd()) {
            socket.write(file.read(64 * 1024));
            if (!socket.waitForBytesWritten(5000)) {
                return false;
            }
        }

        if (!socket.waitForReadyRead(5000) || !socket.readAll().contains("OK")) {
            return false;
        }

        socket.disconnectFromHost();
        return true;
    };

    QString remoteTarget = currentRemotePath.isEmpty() ? info.fileName() : QDir(currentRemotePath).filePath(info.fileName());

    if (info.isDir()) {
        statusLabel->setText("正在发送目录...");

        if (!makeRemoteDir(remoteTarget)) {
            statusLabel->setText("目录发送失败");
            QMessageBox::warning(this, "提示", "远端目录创建失败");
            return;
        }

        QDir rootDir(info.absoluteFilePath());
        QDirIterator dirIt(info.absoluteFilePath(), QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (dirIt.hasNext()) {
            QString localDirPath = dirIt.next();
            QString relativePath = rootDir.relativeFilePath(localDirPath);
            QString remoteDirPath = QDir(remoteTarget).filePath(relativePath);

            if (!makeRemoteDir(remoteDirPath)) {
                statusLabel->setText("目录发送失败");
                QMessageBox::warning(this, "提示", "远端子目录创建失败");
                return;
            }
        }

        int fileCount = 0;
        QDirIterator fileIt(info.absoluteFilePath(), QDir::Files, QDirIterator::Subdirectories);
        while (fileIt.hasNext()) {
            QString localFilePath = fileIt.next();
            QString relativePath = rootDir.relativeFilePath(localFilePath);
            QString remoteFilePath = QDir(remoteTarget).filePath(relativePath);

            if (!sendOneFile(localFilePath, remoteFilePath)) {
                statusLabel->setText("目录发送失败");
                QMessageBox::warning(this, "提示", "目录中的文件发送失败");
                return;
            }

            fileCount++;
        }

        statusLabel->setText("目录发送成功");
        QMessageBox::information(this, "完成", QString("目录发送成功，共发送 %1 个文件").arg(fileCount));
    } else {
        statusLabel->setText("正在发送文件...");

        if (!sendOneFile(info.absoluteFilePath(), remoteTarget)) {
            statusLabel->setText("文件发送失败");
            QMessageBox::warning(this, "提示", "文件发送失败");
            return;
        }

        statusLabel->setText("文件发送成功");
        QMessageBox::information(this, "完成", "文件发送成功");
    }
}

void MainWindow::refreshFiles()
{
    if (ipEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "提示", "请输入接收端 IP");
        return;
    }

    QTcpSocket socket;
    socket.setProxy(QNetworkProxy::NoProxy);
    statusLabel->setText("正在获取远端目录...");
    socket.connectToHost(ipEdit->text().trimmed(), portEdit->text().toUShort());

    if (!socket.waitForConnected(5000)) {
        statusLabel->setText("连接失败");
        QMessageBox::warning(this, "提示", "连接接收端失败");
        return;
    }

    QByteArray header;
    QDataStream headerStream(&header, QIODevice::WriteOnly);
    headerStream.setVersion(QDataStream::Qt_5_0);
    headerStream << QString("LIST") << currentRemotePath;

    QDataStream socketStream(&socket);
    socketStream.setVersion(QDataStream::Qt_5_0);
    socketStream << static_cast<quint32>(header.size());
    socket.write(header);
    socket.waitForBytesWritten(3000);

    while (socket.bytesAvailable() < static_cast<qint64>(sizeof(quint32))) {
        if (!socket.waitForReadyRead(5000)) {
            statusLabel->setText("获取列表失败");
            QMessageBox::warning(this, "提示", "没有收到目录列表");
            return;
        }
    }

    quint32 replySize = 0;
    socketStream >> replySize;

    while (socket.bytesAvailable() < replySize) {
        if (!socket.waitForReadyRead(5000)) {
            statusLabel->setText("获取列表失败");
            QMessageBox::warning(this, "提示", "目录列表接收不完整");
            return;
        }
    }

    QByteArray reply = socket.read(replySize);
    QDataStream replyStream(&reply, QIODevice::ReadOnly);
    replyStream.setVersion(QDataStream::Qt_5_0);

    QStringList files;
    replyStream >> currentRemotePath >> files;

    remotePathLabel->setText(currentRemotePath);
    remoteList->clear();
    remoteList->addItems(files);
    statusLabel->setText("远端目录已刷新");
    socket.disconnectFromHost();
}

void MainWindow::downloadFile()
{
    if (!remoteList->currentItem()) {
        QMessageBox::warning(this, "提示", "请先选择远端文件");
        return;
    }

    if (ipEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "提示", "请输入接收端 IP");
        return;
    }

    QString remoteFileName = remoteList->currentItem()->text();
    if (remoteFileName == "../" || remoteFileName.endsWith("/")) {
        QMessageBox::warning(this, "提示", "当前选中的是目录，请先进入目录");
        return;
    }

    QString remoteFilePath = currentRemotePath.isEmpty() ? remoteFileName : QDir(currentRemotePath).filePath(remoteFileName);
    QTcpSocket socket;
    socket.setProxy(QNetworkProxy::NoProxy);
    statusLabel->setText("正在请求下载...");
    socket.connectToHost(ipEdit->text().trimmed(), portEdit->text().toUShort());

    if (!socket.waitForConnected(5000)) {
        statusLabel->setText("连接失败");
        QMessageBox::warning(this, "提示", "连接接收端失败");
        return;
    }

    QByteArray header;
    QDataStream headerStream(&header, QIODevice::WriteOnly);
    headerStream.setVersion(QDataStream::Qt_5_0);
    headerStream << QString("GET") << remoteFilePath;

    QDataStream socketStream(&socket);
    socketStream.setVersion(QDataStream::Qt_5_0);
    socketStream << static_cast<quint32>(header.size());
    socket.write(header);
    socket.waitForBytesWritten(3000);

    while (socket.bytesAvailable() < static_cast<qint64>(sizeof(quint32))) {
        if (!socket.waitForReadyRead(5000)) {
            statusLabel->setText("下载失败");
            QMessageBox::warning(this, "提示", "没有收到文件信息");
            return;
        }
    }

    quint32 replySize = 0;
    socketStream >> replySize;

    while (socket.bytesAvailable() < replySize) {
        if (!socket.waitForReadyRead(5000)) {
            statusLabel->setText("下载失败");
            QMessageBox::warning(this, "提示", "文件信息接收不完整");
            return;
        }
    }

    QByteArray reply = socket.read(replySize);
    QDataStream replyStream(&reply, QIODevice::ReadOnly);
    replyStream.setVersion(QDataStream::Qt_5_0);

    qint64 fileSize = -1;
    replyStream >> fileSize;
    if (fileSize < 0) {
        statusLabel->setText("下载失败");
        QMessageBox::warning(this, "提示", "远端文件不存在");
        return;
    }

    QString savePath = QFileDialog::getSaveFileName(this, "保存文件", remoteFileName);
    if (savePath.isEmpty()) {
        statusLabel->setText("已取消下载");
        return;
    }

    QFile file(savePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        statusLabel->setText("下载失败");
        QMessageBox::warning(this, "提示", "本地文件打开失败");
        return;
    }

    qint64 received = 0;
    while (received < fileSize) {
        if (socket.bytesAvailable() == 0 && !socket.waitForReadyRead(5000)) {
            file.close();
            statusLabel->setText("下载失败");
            QMessageBox::warning(this, "提示", "文件接收失败");
            return;
        }

        QByteArray data = socket.read(qMin<qint64>(64 * 1024, fileSize - received));
        if (!data.isEmpty()) {
            file.write(data);
            received += data.size();
        }
    }

    file.close();
    statusLabel->setText("下载成功");
    QMessageBox::information(this, "完成", "文件下载成功");
    socket.disconnectFromHost();
}

void MainWindow::openRemoteItem(QListWidgetItem *item)
{
    QString name = item->text();

    if (name == "../") {
        currentRemotePath = currentRemotePath.isEmpty() ? QString() : QDir(currentRemotePath).absoluteFilePath("..");
        refreshFiles();
        return;
    }

    if (name.endsWith("/")) {
        name.chop(1);
        currentRemotePath = currentRemotePath.isEmpty() ? name : QDir(currentRemotePath).filePath(name);
        refreshFiles();
        return;
    }

    downloadFile();
}
