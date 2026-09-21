#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>

namespace adoloop {

// 轻量子进程封装：运行外部程序并捕获输出。
// 全部同步等待（带超时），用于波形/ASR 等短任务；
// 长任务（mpv / yt-dlp）由专用类用 QProcess 信号驱动。
class Subprocess {
public:
    struct Result {
        bool ok = false;
        int exitCode = -1;
        QByteArray stdoutData;
        QByteArray stderrData;
        QString errorMessage;
        QString stdoutText() const { return QString::fromUtf8(stdoutData); }
        QString stderrText() const { return QString::fromUtf8(stderrData); }
    };

    // 同步运行；timeoutMs<=0 表示不限时（默认 30s）
    static Result run(const QString &program, const QStringList &args, int timeoutMs = 30000);

    // 同上，但把 input 写入子进程 stdin（行式协议：MeCab / Python 桥批量分词用）
    static Result runWithInput(const QString &program, const QStringList &args,
                               const QByteArray &input, int timeoutMs = 30000);

    // 探测可执行文件是否可用（在 PATH 或指定路径）
    static bool findExecutable(const QString &nameOrPath, QString *resolvedPath = nullptr);
};

} // namespace adoloop
