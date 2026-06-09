#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QWidget>

class QLineEdit;
class QLabel;
class QListWidget;
class QListWidgetItem;

class MainWindow : public QWidget
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void chooseFile();
    void chooseDirectory();
    void sendFile();
    void refreshFiles();
    void downloadFile();
    void openRemoteItem(QListWidgetItem *item);

private:
    QLineEdit *ipEdit;
    QLineEdit *portEdit;
    QLineEdit *fileEdit;
    QLabel *statusLabel;
    QLabel *remotePathLabel;
    QListWidget *remoteList;
    QString currentRemotePath;
};

#endif
