#include "mainwindow.h"

#include <QDataStream>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkProxy>
#include <QPushButton>
#include <QTcpSocket>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle("QtTransfer Sender");
    resize(560, 420);

    QLabel *ipLabel = new QLabel("接收端 IP:");
    QLabel *portLabel = new QLabel("端口:");
    QLabel *fileLabel = new QLabel("文件:");
    QLabel *remoteLabel = new QLabel("远端文件:");

    ipEdit = new QLineEdit("127.0.0.1");
    portEdit = new QLineEdit("8899");
    fileEdit = new QLineEdit;
    fileEdit->setReadOnly(true);
    remoteList = new QListWidget;

    QPushButton *browseButton = new QPushButton("选择文件");
    QPushButton *sendButton = new QPushButton("发送文件");
    QPushButton *refreshButton = new QPushButton("刷新列表");
    QPushButton *downloadButton = new QPushButton("下载选中");

    statusLabel = new QLabel("请选择文件并发送");

    QHBoxLayout *ipLayout = new QHBoxLayout;
    ipLayout->addWidget(ipLabel);
    ipLayout->addWidget(ipEdit);
    ipLayout->addWidget(portLabel);
    ipLayout->addWidget(portEdit);

    QHBoxLayout *fileLayout = new QHBoxLayout;
    fileLayout->addWidget(fileLabel);
    fileLayout->addWidget(fileEdit);
    fileLayout->addWidget(browseButton);

    QHBoxLayout *remoteButtonLayout = new QHBoxLayout;
    remoteButtonLayout->addWidget(refreshButton);
    remoteButtonLayout->addWidget(downloadButton);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(ipLayout);
    mainLayout->addLayout(fileLayout);
    mainLayout->addWidget(remoteLabel);
    mainLayout->addWidget(remoteList);
    mainLayout->addLayout(remoteButtonLayout);
    mainLayout->addWidget(statusLabel);
    mainLayout->addWidget(sendButton);

    setStyleSheet(
        "QWidget { background: #f6f7fb; font-size: 14px; }"
        "QLineEdit { background: white; border: 1px solid #cfd6e4; border-radius: 6px; padding: 6px; }"
        "QPushButton { background: #2f6fed; color: white; border: none; border-radius: 6px; padding: 8px 14px; }"
        "QPushButton:hover { background: #1f5dd8; }"
        "QLabel { color: #243042; }");

    connect(browseButton, &QPushButton::clicked, this, &MainWindow::chooseFile);
    connect(sendButton, &QPushButton::clicked, this, &MainWindow::sendFile);
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::refreshFiles);
    connect(downloadButton, &QPushButton::clicked, this, &MainWindow::downloadFile);
}

void MainWindow::chooseFile()
{
    QString path = QFileDialog::getOpenFileName(this, "选择要发送的文件");

    if (!path.isEmpty()) {
        fileEdit->setText(path);
        statusLabel->setText("文件已选择，可以发送");
    }
}

void MainWindow::sendFile()
{
    if (fileEdit->text().isEmpty()) {
        QMessageBox::warning(this, "提示", "请先选择文件");
        return;
    }

    if (ipEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "提示", "请输入接收端 IP");
        return;
    }

    QFile file(fileEdit->text());
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "提示", "文件打开失败");
        return;
    }

    QTcpSocket socket;
    socket.setProxy(QNetworkProxy::NoProxy);
    statusLabel->setText("正在连接接收端...");
    socket.connectToHost(ipEdit->text().trimmed(), portEdit->text().toUShort());

    if (!socket.waitForConnected(5000)) {
        statusLabel->setText("连接失败");
        QMessageBox::warning(this, "提示", "连接接收端失败");
        return;
    }

    QFileInfo info(file);
    QByteArray header;
    QDataStream headerStream(&header, QIODevice::WriteOnly);
    headerStream.setVersion(QDataStream::Qt_5_0);
    headerStream << QString("PUT") << info.fileName() << static_cast<qint64>(file.size());

    QDataStream socketStream(&socket);
    socketStream.setVersion(QDataStream::Qt_5_0);
    socketStream << static_cast<quint32>(header.size());
    socket.write(header);

    while (!file.atEnd()) {
        socket.write(file.read(64 * 1024));
        if (!socket.waitForBytesWritten(5000)) {
            statusLabel->setText("发送失败");
            QMessageBox::warning(this, "提示", "文件发送失败");
            return;
        }
    }

    statusLabel->setText("文件已发送，等待确认...");
    if (socket.waitForReadyRead(5000) && socket.readAll().contains("OK")) {
        statusLabel->setText("发送成功");
        QMessageBox::information(this, "完成", "文件发送成功");
    } else {
        statusLabel->setText("未收到接收确认");
        QMessageBox::warning(this, "提示", "文件已发送，但未收到确认");
    }

    socket.disconnectFromHost();
}

void MainWindow::refreshFiles()
{
    if (ipEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "提示", "请输入接收端 IP");
        return;
    }

    QTcpSocket socket;
    socket.setProxy(QNetworkProxy::NoProxy);
    statusLabel->setText("正在获取远端文件列表...");
    socket.connectToHost(ipEdit->text().trimmed(), portEdit->text().toUShort());

    if (!socket.waitForConnected(5000)) {
        statusLabel->setText("连接失败");
        QMessageBox::warning(this, "提示", "连接接收端失败");
        return;
    }

    QByteArray header;
    QDataStream headerStream(&header, QIODevice::WriteOnly);
    headerStream.setVersion(QDataStream::Qt_5_0);
    headerStream << QString("LIST");

    QDataStream socketStream(&socket);
    socketStream.setVersion(QDataStream::Qt_5_0);
    socketStream << static_cast<quint32>(header.size());
    socket.write(header);
    socket.waitForBytesWritten(3000);

    while (socket.bytesAvailable() < static_cast<qint64>(sizeof(quint32))) {
        if (!socket.waitForReadyRead(5000)) {
            statusLabel->setText("获取列表失败");
            QMessageBox::warning(this, "提示", "没有收到文件列表");
            return;
        }
    }

    quint32 replySize = 0;
    socketStream >> replySize;

    while (socket.bytesAvailable() < replySize) {
        if (!socket.waitForReadyRead(5000)) {
            statusLabel->setText("获取列表失败");
            QMessageBox::warning(this, "提示", "文件列表接收不完整");
            return;
        }
    }

    QByteArray reply = socket.read(replySize);
    QDataStream replyStream(&reply, QIODevice::ReadOnly);
    replyStream.setVersion(QDataStream::Qt_5_0);

    QStringList files;
    replyStream >> files;

    remoteList->clear();
    remoteList->addItems(files);
    statusLabel->setText("远端文件列表已刷新");
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

    QString remoteFile = remoteList->currentItem()->text();
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
    headerStream << QString("GET") << remoteFile;

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

    QString savePath = QFileDialog::getSaveFileName(this, "保存文件", remoteFile);
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
