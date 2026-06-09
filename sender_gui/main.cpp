#include <QApplication>
#include <QFont>
#include <QFontDatabase>

#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QFont font = app.font();
    QFontDatabase database;
    const QStringList preferredFonts = {
        "Noto Sans CJK SC",
        "WenQuanYi Micro Hei",
        "Microsoft YaHei",
        "SimHei"
    };

    for (const QString &family : preferredFonts) {
        if (database.families().contains(family)) {
            font.setFamily(family);
            app.setFont(font);
            break;
        }
    }

    MainWindow window;
    window.show();
    return app.exec();
}
