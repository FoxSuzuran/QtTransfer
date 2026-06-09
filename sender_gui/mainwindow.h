#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QWidget>

class QLineEdit;
class QLabel;

class MainWindow : public QWidget
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void chooseFile();
    void testConnection();

private:
    QLineEdit *ipEdit;
    QLineEdit *portEdit;
    QLineEdit *fileEdit;
    QLabel *statusLabel;
};

#endif
