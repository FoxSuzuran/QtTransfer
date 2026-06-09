#include "mainwindow.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
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
    QPushButton *testButton = new QPushButton("连接测试");

    statusLabel = new QLabel("请选择文件，并测试接收端连接");

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
    mainLayout->addWidget(testButton);

    setStyleSheet(
        "QWidget { background: #f6f7fb; font-size: 14px; }"
        "QLineEdit { background: white; border: 1px solid #cfd6e4; border-radius: 6px; padding: 6px; }"
        "QPushButton { background: #2f6fed; color: white; border: none; border-radius: 6px; padding: 8px 14px; }"
        "QPushButton:hover { background: #1f5dd8; }"
        "QLabel { color: #243042; }");

    connect(browseButton, &QPushButton::clicked, this, &MainWindow::chooseFile);
    connect(testButton, &QPushButton::clicked, this, &MainWindow::testConnection);
}

void MainWindow::chooseFile()
{
    QString path = QFileDialog::getOpenFileName(this, "选择要发送的文件");
    if (!path.isEmpty()) {
        fileEdit->setText(path);
        statusLabel->setText("文件已选择，可以测试连接");
    }
}

void MainWindow::testConnection()
{
    if (fileEdit->text().isEmpty()) {
        QMessageBox::warning(this, "提示", "请先选择文件");
        return;
    }

    if (ipEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "提示", "请输入接收端 IP");
        return;
    }

    QTcpSocket socket;
    statusLabel->setText("正在测试连接...");
    socket.connectToHost(ipEdit->text().trimmed(), portEdit->text().toUShort());

    if (!socket.waitForConnected(5000)) {
        statusLabel->setText("连接失败");
        QMessageBox::warning(this, "提示", "连接接收端失败");
        return;
    }

    statusLabel->setText("连接成功，可以进入下一阶段");
    QMessageBox::information(this, "完成", "连接测试成功");
    socket.disconnectFromHost();
}
