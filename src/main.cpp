#include "cli.h"
#include "ui/mainwindow.h"

#include <QApplication>
#include <cstring>

int main(int argc, char** argv)
{
    // QSettings、模型目录（AppDataLocation）都依赖这几个名字，必须在任何读取之前设置
    QCoreApplication::setOrganizationName(QStringLiteral("Jimaku"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("wangchaozhi.github.io"));
    QCoreApplication::setApplicationName(QStringLiteral("Jimaku"));
    QCoreApplication::setApplicationVersion(QStringLiteral(APP_VERSION));

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--cli") == 0)
            return runCli(argc, argv);
    }

    QApplication app(argc, argv);
    QApplication::setApplicationDisplayName(QStringLiteral("Jimaku"));
    MainWindow w;
    w.show();
    return QApplication::exec();
}
