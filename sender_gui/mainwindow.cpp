#include "mainwindow.h"

#include <QDataStream>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkProxy>
#include <QPushButton>
#include <QTcpSocket>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle("QtTransfer Sender");
    resize(520, 220);

    QLabel *ipLabel = new QLabel("接收端 IP:");
    QLabel *portLabel = new QLabel("端口:");
    QLabel *fileLabel = new QLabel("文件:");

    ipEdit = new QLineEdit("127.0.0.1");
    portEdit = new QLineEdit("8899");
    fileEdit = new QLineEdit;
    fileEdit->setReadOnly(true);

    QPushButton *browseButton = new QPushButton("选择文件");
    QPushButton *sendButton = new QPushButton("发送文件");

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

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(ipLayout);
    mainLayout->addLayout(fileLayout);
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
    headerStream << info.fileName() << static_cast<qint64>(file.size());

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
