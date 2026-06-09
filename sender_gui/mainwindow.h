#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QWidget>

class QLineEdit;
class QLabel;
class QListWidget;

class MainWindow : public QWidget
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void chooseFile();
    void sendFile();
    void refreshFiles();
    void downloadFile();

private:
    QLineEdit *ipEdit;
    QLineEdit *portEdit;
    QLineEdit *fileEdit;
    QLabel *statusLabel;
    QListWidget *remoteList;
};

#endif
