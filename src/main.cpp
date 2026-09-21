#include "app/AppController.h"
#include "app/MainWindow.h"
#include "app/Settings.h"
#include "util/CrashLog.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    // 应用元数据放到 QApplication 之前：崩溃日志与数据目录都依赖它
    // （未设置时 QStandardPaths 会退回 exe 文件名，日志就落到别处去了）。
    QCoreApplication::setApplicationName(QStringLiteral("AdoLoop"));
    QCoreApplication::setOrganizationName(QStringLiteral("AdoLoop"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    // 崩溃日志要先于 QApplication 就绪：平台插件加载就发生在 QApplication 构造里，
    // 失败时 Qt 走 qFatal→abort，那条消息只有提前挂上处理器才留得住（M15）。
    adoloop::Settings::initialize();
    adoloop::crashlog::install(adoloop::Settings::instance().dataDir());

    QApplication app(argc, argv);

    adoloop::MainWindow win;
    win.show();

    adoloop::AppController ctrl(&win);
    ctrl.initialize();

    const int code = app.exec();
    adoloop::crashlog::logExit(code);
    return code;
}
