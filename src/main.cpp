#include "cli.h"
#include "ui/mainwindow.h"

#include <QApplication>
#include <QFileInfo>
#include <QIcon>
#include <QTimer>
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
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/resources/icons/jimaku-256.png")));
    MainWindow w;
    w.show();
    // Jimaku 视频.mp4：启动后直接开始处理
    const QStringList args = QApplication::arguments();
    if (args.size() > 1 && QFileInfo(args.last()).isFile())
        QTimer::singleShot(0, &w, [&w, path = args.last()] { w.openFile(QFileInfo(path).absoluteFilePath()); });
    return QApplication::exec();
}
