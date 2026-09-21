#include "Subprocess.h"

#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QEventLoop>
#include <QCoreApplication>

namespace adoloop {

Subprocess::Result Subprocess::run(const QString &program, const QStringList &args, int timeoutMs)
{
    return runWithInput(program, args, QByteArray(), timeoutMs);
}

Subprocess::Result Subprocess::runWithInput(const QString &program, const QStringList &args,
                                            const QByteArray &input, int timeoutMs)
{
    Result r;
    QProcess p;
    p.setProgram(program);
    p.setArguments(args);
    p.setProcessChannelMode(QProcess::SeparateChannels);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    QObject::connect(&p, &QProcess::errorOccurred, &loop, [&](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            r.errorMessage = QStringLiteral("无法启动: %1").arg(program);
        } else if (e == QProcess::Crashed) {
            r.errorMessage = QStringLiteral("进程崩溃: %1").arg(program);
        }
    });

    if (timeoutMs > 0) {
        QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
            r.errorMessage = QStringLiteral("执行超时(%1 ms): %2").arg(timeoutMs).arg(program);
            p.kill();
        });
        timer.start(timeoutMs);
    }

    p.start();
    // 等待启动失败或结束
    const int maxWait = timeoutMs > 0 ? timeoutMs + 5000 : 60000;
    if (!p.waitForStarted(maxWait)) {
        timer.stop();
        r.errorMessage = r.errorMessage.isEmpty() ? QStringLiteral("启动失败: %1").arg(program) : r.errorMessage;
        return r;
    }
    QObject::connect(&p, &QProcess::finished, &loop, [&](int code) { r.exitCode = code; });

    // 行式协议：一次性写入 stdin 并关闭写端，让子进程（MeCab/Python 桥）读到 EOF 后退出
    if (!input.isEmpty()) {
        p.write(input);
        p.waitForBytesWritten(2000);
    }
    p.closeWriteChannel();

    // 边跑边收集输出
    QObject::connect(&p, &QProcess::readyReadStandardOutput, &loop, [&]() {
        r.stdoutData += p.readAllStandardOutput();
    });
    QObject::connect(&p, &QProcess::readyReadStandardError, &loop, [&]() {
        r.stderrData += p.readAllStandardError();
    });

    loop.exec();
    timer.stop();

    r.stdoutData += p.readAllStandardOutput();
    r.stderrData += p.readAllStandardError();
    r.ok = (p.state() == QProcess::NotRunning && r.exitCode == 0 && r.errorMessage.isEmpty());
    return r;
}

bool Subprocess::findExecutable(const QString &nameOrPath, QString *resolvedPath)
{
    QString candidate = nameOrPath.trimmed();
    if (candidate.isEmpty())
        return false;

    // 直接给路径
    if (candidate.contains('/') || candidate.contains('\\')) {
        if (QFileInfo::exists(candidate)) {
            if (resolvedPath)
                *resolvedPath = QFileInfo(candidate).absoluteFilePath();
            return true;
        }
        return false;
    }

    // 在 PATH 中查找（Windows 追加 .exe）
    const QStringList pathDirs = qEnvironmentVariable("PATH").split(';', Qt::SkipEmptyParts);
    QStringList names;
    names << candidate;
#ifdef Q_OS_WIN
    if (!candidate.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive))
        names << candidate + QLatin1String(".exe");
#endif
    for (const QString &dir : pathDirs) {
        for (const QString &name : names) {
            const QString full = QDir(dir).filePath(name);
            if (QFileInfo::exists(full)) {
                if (resolvedPath)
                    *resolvedPath = QFileInfo(full).absoluteFilePath();
                return true;
            }
        }
    }
    return false;
}

} // namespace adoloop
