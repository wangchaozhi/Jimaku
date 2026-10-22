#include "cli.h"
#include "ui/mainwindow.h"

#include <QApplication>
#include <QFileInfo>
#include <QIcon>
#include <QMessageBox>
#include <QTimer>
#include <cstring>

#ifdef Q_OS_MACOS
namespace {

// 新版 macOS 上 Qt 取不到系统的提示图标，对话框里显示一个空白占位图标；
// 系统自己的提示框显示的是程序图标，这里照做
class MessageBoxIcon : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Show) {
            auto* box = qobject_cast<QMessageBox*>(watched);
            if (box && box->icon() != QMessageBox::NoIcon)
                box->setIconPixmap(QApplication::windowIcon().pixmap(64, 64));
        }
        return false;
    }
};

} // namespace
#endif

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
#ifdef Q_OS_MACOS
    app.installEventFilter(new MessageBoxIcon(&app));
#endif
    MainWindow w;
    w.show();
    // Jimaku 视频.mp4：启动后直接开始处理
    const QStringList args = QApplication::arguments();
    if (args.size() > 1 && QFileInfo(args.last()).isFile())
        QTimer::singleShot(0, &w, [&w, path = args.last()] { w.openFile(QFileInfo(path).absoluteFilePath()); });
    return QApplication::exec();
}
