#include "mainwindow.h"

#include <QApplication>
#include <QDataStream>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <functional>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QNetworkProxy>
#include <QProgressBar>
#include <QPushButton>
#include <QTcpSocket>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle("QtTransfer Sender");
    resize(620, 480);

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
    progressBar = new QProgressBar;
    progressBar->setRange(0, 100);
    progressBar->setValue(0);

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
    mainLayout->addWidget(progressBar);
    mainLayout->addWidget(sendButton);

    setStyleSheet(
        "QWidget { background: #f6f7fb; font-size: 14px; }"
        "QLineEdit { background: white; border: 1px solid #cfd6e4; border-radius: 6px; padding: 6px; }"
        "QListWidget { background: white; border: 1px solid #cfd6e4; border-radius: 6px; }"
        "QProgressBar { background: white; border: 1px solid #cfd6e4; border-radius: 6px; text-align: center; }"
        "QProgressBar::chunk { background: #2f6fed; border-radius: 5px; }"
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

    qint64 totalBytes = 0;
    if (info.isDir()) {
        QDirIterator totalIt(info.absoluteFilePath(), QDir::Files, QDirIterator::Subdirectories);
        while (totalIt.hasNext()) {
            totalIt.next();
            totalBytes += totalIt.fileInfo().size();
        }
    } else {
        totalBytes = info.size();
    }

    qint64 sentBytes = 0;
    progressBar->setValue(0);
    QApplication::processEvents();

    auto updateProgress = [&](qint64 currentBytes) {
        if (totalBytes <= 0) {
            progressBar->setValue(100);
        } else {
            progressBar->setValue(static_cast<int>(currentBytes * 100 / totalBytes));
        }
        QApplication::processEvents();
    };

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

        statusLabel->setText(QString("正在发送 %1").arg(QFileInfo(localPath).fileName()));
        QApplication::processEvents();

        const qint64 chunkLimit = 10 * 1024 * 1024;
        const qint64 chunkSize = 1024 * 1024;

        if (file.size() > chunkLimit) {
            int chunkCount = static_cast<int>((file.size() + chunkSize - 1) / chunkSize);

            for (int chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex) {
                QByteArray chunkData = file.read(chunkSize);
                if (chunkData.isEmpty() && file.size() > 0) {
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
                headerStream << QString("PUT_CHUNK")
                             << remotePath
                             << static_cast<qint64>(file.size())
                             << chunkCount
                             << chunkIndex
                             << static_cast<qint64>(chunkData.size());

                QDataStream socketStream(&socket);
                socketStream.setVersion(QDataStream::Qt_5_0);
                socketStream << static_cast<quint32>(header.size());
                socket.write(header);
                if (!socket.waitForBytesWritten(3000)) {
                    return false;
                }

                socket.write(chunkData);
                if (!socket.waitForBytesWritten(5000)) {
                    return false;
                }

                if (!socket.waitForReadyRead(5000) || !socket.readAll().contains("OK")) {
                    return false;
                }

                sentBytes += chunkData.size();
                statusLabel->setText(QString("正在发送分块 %1/%2").arg(chunkIndex + 1).arg(chunkCount));
                updateProgress(sentBytes);
                socket.disconnectFromHost();
            }

            return true;
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
            QByteArray data = file.read(64 * 1024);
            socket.write(data);
            if (!socket.waitForBytesWritten(5000)) {
                return false;
            }

            sentBytes += data.size();
            updateProgress(sentBytes);
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
        updateProgress(totalBytes);
        QMessageBox::information(this, "完成", QString("目录发送成功，共发送 %1 个文件").arg(fileCount));
    } else {
        statusLabel->setText("正在发送文件...");

        if (!sendOneFile(info.absoluteFilePath(), remoteTarget)) {
            statusLabel->setText("文件发送失败");
            QMessageBox::warning(this, "提示", "文件发送失败");
            return;
        }

        statusLabel->setText("文件发送成功");
        updateProgress(totalBytes);
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
    progressBar->setValue(0);
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
    if (remoteFileName == "../") {
        QMessageBox::warning(this, "提示", "请先选择远端目录或文件");
        return;
    }

    auto listRemoteDir = [&](const QString &remotePath, QString *currentPath, QStringList *files) {
        QTcpSocket socket;
        socket.setProxy(QNetworkProxy::NoProxy);
        socket.connectToHost(ipEdit->text().trimmed(), portEdit->text().toUShort());

        if (!socket.waitForConnected(5000)) {
            return false;
        }

        QByteArray header;
        QDataStream headerStream(&header, QIODevice::WriteOnly);
        headerStream.setVersion(QDataStream::Qt_5_0);
        headerStream << QString("LIST") << remotePath;

        QDataStream socketStream(&socket);
        socketStream.setVersion(QDataStream::Qt_5_0);
        socketStream << static_cast<quint32>(header.size());
        socket.write(header);
        socket.waitForBytesWritten(3000);

        while (socket.bytesAvailable() < static_cast<qint64>(sizeof(quint32))) {
            if (!socket.waitForReadyRead(5000)) {
                return false;
            }
        }

        quint32 replySize = 0;
        socketStream >> replySize;

        while (socket.bytesAvailable() < replySize) {
            if (!socket.waitForReadyRead(5000)) {
                return false;
            }
        }

        QByteArray reply = socket.read(replySize);
        QDataStream replyStream(&reply, QIODevice::ReadOnly);
        replyStream.setVersion(QDataStream::Qt_5_0);
        replyStream >> *currentPath >> *files;
        socket.disconnectFromHost();
        return true;
    };

    auto downloadOneFile = [&](const QString &remoteFilePath, const QString &savePath, int finishedFiles, int totalFiles) {
        QTcpSocket socket;
        socket.setProxy(QNetworkProxy::NoProxy);
        statusLabel->setText(QString("正在下载 %1").arg(QFileInfo(savePath).fileName()));
        socket.connectToHost(ipEdit->text().trimmed(), portEdit->text().toUShort());

        if (!socket.waitForConnected(5000)) {
            return false;
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
                return false;
            }
        }

        quint32 replySize = 0;
        socketStream >> replySize;

        while (socket.bytesAvailable() < replySize) {
            if (!socket.waitForReadyRead(5000)) {
                return false;
            }
        }

        QByteArray reply = socket.read(replySize);
        QDataStream replyStream(&reply, QIODevice::ReadOnly);
        replyStream.setVersion(QDataStream::Qt_5_0);

        qint64 fileSize = -1;
        replyStream >> fileSize;
        if (fileSize < 0) {
            return false;
        }

        QFileInfo saveInfo(savePath);
        QDir().mkpath(saveInfo.path());

        QFile file(savePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return false;
        }

        qint64 received = 0;
        while (received < fileSize) {
            if (socket.bytesAvailable() == 0 && !socket.waitForReadyRead(5000)) {
                file.close();
                return false;
            }

            QByteArray data = socket.read(qMin<qint64>(64 * 1024, fileSize - received));
            if (!data.isEmpty()) {
                file.write(data);
                received += data.size();

                if (totalFiles <= 0) {
                    progressBar->setValue(fileSize <= 0 ? 100 : static_cast<int>(received * 100 / fileSize));
                } else {
                    double current = fileSize <= 0 ? 1.0 : static_cast<double>(received) / fileSize;
                    progressBar->setValue(static_cast<int>((finishedFiles + current) * 100 / totalFiles));
                }
                QApplication::processEvents();
            }
        }

        file.close();
        socket.disconnectFromHost();
        return true;
    };

    progressBar->setValue(0);
    QApplication::processEvents();

    if (!remoteFileName.endsWith("/")) {
        QString remoteFilePath = currentRemotePath.isEmpty() ? remoteFileName : QDir(currentRemotePath).filePath(remoteFileName);
        QString savePath = QFileDialog::getSaveFileName(this, "保存文件", remoteFileName);
        if (savePath.isEmpty()) {
            statusLabel->setText("已取消下载");
            return;
        }

        if (!downloadOneFile(remoteFilePath, savePath, 0, 1)) {
            statusLabel->setText("下载失败");
            QMessageBox::warning(this, "提示", "文件下载失败");
            return;
        }

        statusLabel->setText("下载成功");
        progressBar->setValue(100);
        QMessageBox::information(this, "完成", "文件下载成功");
        return;
    }

    QString dirName = remoteFileName;
    dirName.chop(1);
    QString remoteDirPath = currentRemotePath.isEmpty() ? dirName : QDir(currentRemotePath).filePath(dirName);
    QString localBasePath = QFileDialog::getExistingDirectory(this, "选择保存目录");
    if (localBasePath.isEmpty()) {
        statusLabel->setText("已取消下载");
        return;
    }

    QString localRootPath = QDir(localBasePath).filePath(dirName);
    QDir().mkpath(localRootPath);

    int totalFiles = 0;
    std::function<bool(const QString &)> countRemoteFiles = [&](const QString &path) {
        QString currentPath;
        QStringList files;
        if (!listRemoteDir(path, &currentPath, &files)) {
            return false;
        }

        for (QString fileName : files) {
            if (fileName == "../") {
                continue;
            }

            if (fileName.endsWith("/")) {
                fileName.chop(1);
                if (!countRemoteFiles(QDir(currentPath).filePath(fileName))) {
                    return false;
                }
            } else {
                totalFiles++;
            }
        }

        return true;
    };

    std::function<bool(const QString &, const QString &, int &)> downloadRemoteDir =
        [&](const QString &remotePath, const QString &localPath, int &finishedFiles) {
            QDir().mkpath(localPath);

            QString currentPath;
            QStringList files;
            if (!listRemoteDir(remotePath, &currentPath, &files)) {
                return false;
            }

            for (QString fileName : files) {
                if (fileName == "../") {
                    continue;
                }

                if (fileName.endsWith("/")) {
                    fileName.chop(1);
                    if (!downloadRemoteDir(QDir(currentPath).filePath(fileName), QDir(localPath).filePath(fileName), finishedFiles)) {
                        return false;
                    }
                } else {
                    if (!downloadOneFile(QDir(currentPath).filePath(fileName), QDir(localPath).filePath(fileName), finishedFiles, totalFiles)) {
                        return false;
                    }

                    finishedFiles++;
                    progressBar->setValue(totalFiles <= 0 ? 100 : static_cast<int>(finishedFiles * 100 / totalFiles));
                    QApplication::processEvents();
                }
            }

            return true;
        };

    statusLabel->setText("正在统计目录...");
    if (!countRemoteFiles(remoteDirPath)) {
        statusLabel->setText("目录下载失败");
        QMessageBox::warning(this, "提示", "远端目录读取失败");
        return;
    }

    int finishedFiles = 0;
    statusLabel->setText("正在下载目录...");
    if (!downloadRemoteDir(remoteDirPath, localRootPath, finishedFiles)) {
        statusLabel->setText("目录下载失败");
        QMessageBox::warning(this, "提示", "目录下载失败");
        return;
    }

    statusLabel->setText("目录下载成功");
    progressBar->setValue(100);
    QMessageBox::information(this, "完成", QString("目录下载成功，共下载 %1 个文件").arg(finishedFiles));
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
