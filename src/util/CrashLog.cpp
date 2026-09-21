#include "CrashLog.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QSysInfo>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <mutex>
#include <typeinfo>

#ifdef Q_OS_WIN
#  include <io.h>
#  include <process.h>
#  include <windows.h>
#endif

namespace adoloop::crashlog {

namespace {

// 会改变「Qt 消息 → 进程死亡」语义的环境变量：QT_FATAL_WARNINGS / QT_FATAL_CRITICALS
// 会把 warning/critical 直接升级成 qFatal→abort（排查这类崩溃时它们是最重要的上下文），
// QT_PLUGIN_PATH / QT_QPA_PLATFORM_PLUGIN_PATH 会改变插件加载来源。
const char *kNotableEnvVars[] = {
    "QT_FATAL_WARNINGS", "QT_FATAL_CRITICALS", "QT_DEBUG_PLUGINS", "QT_LOGGING_RULES",
    "QT_QPA_PLATFORM", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QT_MEDIA_BACKEND",
};

struct State {
    QString logPath;
    QString terminatePath;
    qint64  limit = 2 * 1024 * 1024;
    qint64  bytes = 0;
    bool    installed = false;
    QtMessageHandler prev = nullptr;
};

State g;
std::mutex g_mutex;
std::FILE *g_file = nullptr;

#ifdef Q_OS_WIN
const wchar_t *widePath(const QString &p)
{
    return reinterpret_cast<const wchar_t *>(p.utf16());
}
#endif

qint64 currentPid()
{
#ifdef Q_OS_WIN
    return qint64(_getpid());
#else
    return qint64(::getpid());
#endif
}

// 可执行文件路径：**不能用 QCoreApplication::applicationFilePath()** ——
// 那在 QApplication 构造之前调用会打印 "Please instantiate the QApplication object first"
// 警告（而 QT_FATAL_WARNINGS=1 时这条警告会直接把进程 abort 掉）。
QString executablePath()
{
#ifdef Q_OS_WIN
    constexpr DWORD kBufSize = 2048;
    wchar_t buf[kBufSize];
    const DWORD n = ::GetModuleFileNameW(nullptr, buf, kBufSize);
    if (n > 0 && n < kBufSize)
        return QString::fromWCharArray(buf, int(n));
#endif
    return QStringLiteral("(未知)");
}

QString appVersionText()
{
    const QString v = QCoreApplication::instance() ? QCoreApplication::applicationVersion()
                                                   : QString();
    return v.isEmpty() ? QStringLiteral("(未设置)") : v;
}

std::FILE *openAppend(const QString &path)
{
#ifdef Q_OS_WIN
    return _wfopen(widePath(path), L"ab");
#else
    return std::fopen(path.toLocal8Bit().constData(), "ab");
#endif
}

// 由地址反查「所属模块的文件名 + 模块内偏移」。用 VirtualQuery + GetModuleFileNameW，
// 不依赖 DbgHelp / 符号文件（本机没有任何调试器，也不允许下载）。
bool moduleOfAddress(void *addr, QString *name, quint64 *rva)
{
#ifdef Q_OS_WIN
    MEMORY_BASIC_INFORMATION mbi;
    if (::VirtualQuery(addr, &mbi, sizeof(mbi)) == 0)
        return false;
    const HMODULE mod = static_cast<HMODULE>(mbi.AllocationBase);
    if (!mod)
        return false;
    constexpr DWORD kBuf = 2048;
    wchar_t buf[kBuf];
    const DWORD n = ::GetModuleFileNameW(mod, buf, kBuf);
    if (n == 0 || n >= kBuf)
        return false;
    if (name)
        *name = QFileInfo(QString::fromWCharArray(buf, int(n))).fileName();
    if (rva)
        *rva = quint64(quintptr(addr)) - quint64(quintptr(mod));
    return true;
#else
    Q_UNUSED(addr);
    Q_UNUSED(name);
    Q_UNUSED(rva);
    return false;
#endif
}

void closeLocked()
{
    if (g_file) {
        std::fclose(g_file);
        g_file = nullptr;
    }
}

// 不做加锁、不碰全局 FILE* 的「裸追加」：只在拿不到锁（例如 terminate 打断了正在写入的
// 线程）时兜底使用。语义上宁可交错一行，也不能在这里阻塞或二次崩溃。
void appendRaw(const QString &path, const QByteArray &bytes)
{
    if (path.isEmpty() || bytes.isEmpty())
        return;
    if (std::FILE *f = openAppend(path)) {
        std::fwrite(bytes.constData(), 1, size_t(bytes.size()), f);
        std::fflush(f);
        std::fclose(f);
    }
}

// 调用时必须已持锁
void rotateLocked()
{
    closeLocked();
    const QString backup = g.logPath + QLatin1String(".1");
#ifdef Q_OS_WIN
    ::_wremove(widePath(backup));
    ::_wrename(widePath(g.logPath), widePath(backup));
#else
    std::remove(backup.toLocal8Bit().constData());
    std::rename(g.logPath.toLocal8Bit().constData(), backup.toLocal8Bit().constData());
#endif
    g_file = openAppend(g.logPath);
    g.bytes = 0;
    const QFileInfo fi(g.logPath);
    if (fi.exists())
        g.bytes = fi.size();
}

// 调用时必须已持锁
void writeLocked(const QByteArray &bytes, bool sync)
{
    if (!g_file)
        g_file = openAppend(g.logPath);
    if (!g_file)
        return;
    if (shouldRotate(g.bytes, qint64(bytes.size()), g.limit))
        rotateLocked();
    const size_t written = std::fwrite(bytes.constData(), 1, size_t(bytes.size()), g_file);
    g.bytes += qint64(written);
    if (sync)
        std::fflush(g_file);
}

void emitRecord(const Record &r)
{
    const QByteArray bytes = formatLine(r).toUtf8();
    if (bytes.isEmpty())
        return;

    // warning 及以上同步刷盘：abort/terminate 不会冲刷 stdio 缓冲，缓冲过的行等于没写
    const bool sync = r.fatal || r.level >= QtWarningMsg;
    {
        std::unique_lock<std::mutex> lock(g_mutex, std::try_to_lock);
        if (lock.owns_lock())
            writeLocked(bytes, sync);
        else
            appendRaw(g.logPath, bytes);
    }
    // 致命级别再单独留一份标记文件：主日志被占用/写不进去时也还有痕迹
    if (r.fatal)
        appendRaw(g.terminatePath, bytes);
}

void messageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg) noexcept
{
    try {
        Record r;
        r.epochMs = QDateTime::currentMSecsSinceEpoch();
        r.pid = currentPid();
        r.level = int(type);
        r.category = QString::fromLatin1(ctx.category ? ctx.category : "");
        r.file = QString::fromLatin1(ctx.file ? ctx.file : "");
        r.line = ctx.line;
        r.message = msg;
        r.fatal = (type == QtFatalMsg);
        if (r.fatal) {
            // 致命消息：把调用栈一起记下来（module+RVA 指纹）。没有符号也能和
            // 「已知原因的对照实验」逐帧比对，从而区分是哪条 qFatal 路径。
            r.message += QStringLiteral("\n调用栈（module+RVA）：\n") + captureStackText(1);
        }
        emitRecord(r);

        // 保持既有诊断路径：原处理器（通常为 Qt 默认的 stderr 输出）优先，没有就自己打 stderr
        if (g.prev) {
            g.prev(type, ctx, msg);
        } else {
            const QByteArray out = formatLine(r).toUtf8();
            std::fwrite(out.constData(), 1, size_t(out.size()), stderr);
            std::fflush(stderr);
        }
    } catch (...) {
        // 记录失败也不能把异常抛回 Qt 内部
    }
    // 注意：致命级别的 abort() 由 Qt 在处理器返回后自己调用，这里不做任何终止动作
}

void terminateHandler() noexcept
{
    try {
        // 记录异常类型：拿不到就静默降级，绝不在这里再抛/再崩
        QString type;
        bool hadException = false;
        try {
            const std::exception_ptr e = std::current_exception();
            if (e) {
                hadException = true;
                try {
                    std::rethrow_exception(e);
                } catch (const std::exception &ex) {
                    type = QString::fromLatin1(typeid(ex).name());
                } catch (...) {
                    type = QStringLiteral("(非 std::exception 派生)");
                }
            }
        } catch (...) {
            type.clear();
            hadException = false;
        }

        Record r;
        r.epochMs = QDateTime::currentMSecsSinceEpoch();
        r.pid = currentPid();
        r.level = QtFatalMsg;
        r.category = QStringLiteral("terminate");
        r.message = terminateText(type, hadException) + QStringLiteral("\n调用栈（module+RVA）：\n")
                    + captureStackText(1);
        r.fatal = true;
        emitRecord(r);
    } catch (...) {
        // 兜底：终止处理器绝不允许抛出（会变成二次 terminate）
    }
    // 处理器返回后由 C++ 运行时调用 abort()（不自行 abort，避免二次终止）
}

void emitBanner()
{
    {
        Record r;
        r.epochMs = QDateTime::currentMSecsSinceEpoch();
        r.pid = currentPid();
        r.level = QtInfoMsg;
        r.category = QStringLiteral("app");
        r.message = QStringLiteral("AdoLoop %1 启动：qt=%2, exe=%3, cwd=%4, 系统=%5")
                        .arg(appVersionText(), QString::fromLatin1(qVersion()), executablePath(),
                             QDir::currentPath(), QSysInfo::prettyProductName());
        emitRecord(r);
    }

    QStringList notable;
    for (const char *name : kNotableEnvVars) {
        const QString v = qEnvironmentVariable(name);
        if (!v.isEmpty())
            notable << QStringLiteral("%1=%2").arg(QString::fromLatin1(name), v);
    }
    if (!notable.isEmpty()) {
        Record r;
        r.epochMs = QDateTime::currentMSecsSinceEpoch();
        r.pid = currentPid();
        r.level = QtWarningMsg;
        r.category = QStringLiteral("env");
        r.message = QStringLiteral("环境变量：%1（QT_FATAL_WARNINGS/QT_FATAL_CRITICALS 会把 warning/critical "
                                   "升级为 abort）").arg(notable.join(QStringLiteral("; ")));
        emitRecord(r);
    }
}

// 自检开关（ADOLOOP_LOG_SELFTEST=1）：验证「日志链路真的通」用，默认关闭、不参与业务逻辑。
// 只发 warning/critical，不发 fatal —— 绝不能为了自检把进程故意搞崩。
void runSelfTestIfRequested()
{
    if (qEnvironmentVariableIsEmpty("ADOLOOP_LOG_SELFTEST"))
        return;
    qWarning().noquote() << QStringLiteral("日志自检：这是一条 warning（ADOLOOP_LOG_SELFTEST=1）");
    qCritical().noquote() << QStringLiteral("日志自检：这是一条 critical");
}

} // namespace

// ---------------------------------------------------------------------------
// 纯函数（可离线单测）
// ---------------------------------------------------------------------------

QString levelName(int level)
{
    switch (level) {
    case QtDebugMsg:    return QStringLiteral("debug");
    case QtInfoMsg:     return QStringLiteral("info");
    case QtWarningMsg:  return QStringLiteral("warning");
    case QtCriticalMsg: return QStringLiteral("critical");
    case QtFatalMsg:    return QStringLiteral("fatal");
    default:            return QStringLiteral("level%1").arg(level);
    }
}

QString formatLine(const Record &r)
{
    QString message = r.message;
    message.replace(QLatin1Char('\r'), QString());
    // 多行消息折成缩进续行：保证「一条记录 = 一行」，方便 grep 与按行解析
    message.replace(QLatin1Char('\n'), QStringLiteral("\n    | "));

    QString line = QStringLiteral("[%1] [%2] [%3]")
                       .arg(QDateTime::fromMSecsSinceEpoch(r.epochMs)
                                .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")))
                       .arg(r.pid)
                       .arg(levelName(r.level));
    if (!r.category.isEmpty())
        line += QStringLiteral(" [%1]").arg(r.category);
    line += QLatin1Char(' ');
    line += message;
    if (!r.file.isEmpty())
        line += QStringLiteral(" (%1:%2)").arg(r.file, QString::number(r.line));
    line += QLatin1Char('\n');
    return line;
}

bool shouldRotate(qint64 currentBytes, qint64 incomingBytes, qint64 limitBytes)
{
    if (limitBytes <= 0)
        return false;
    return currentBytes + incomingBytes > limitBytes;
}

QString terminateText(const QString &exceptionType, bool hadException)
{
    if (!hadException)
        return QStringLiteral("terminate called: 没有活动异常（noexcept 违规 / 纯虚调用 / "
                              "直接调用 std::terminate）");
    if (exceptionType.isEmpty())
        return QStringLiteral("terminate called: 未处理异常（类型未知）");
    return QStringLiteral("terminate called: 未处理异常 %1").arg(exceptionType);
}

QString formatFrame(const QString &moduleName, quint64 rva)
{
    return QStringLiteral("%1+0x%2").arg(moduleName, QString::number(rva, 16));
}

QString captureStackText(int skipFrames)
{
#ifdef Q_OS_WIN
    constexpr USHORT kMaxFrames = 48;
    void *frames[kMaxFrames] = {};
    // 跳过本函数自身 + 调用方指定层数；kernel32 的 CaptureStackBackTrace 在 x64 上走真实栈回溯
    const USHORT count = ::CaptureStackBackTrace(DWORD(skipFrames) + 1, kMaxFrames, frames, nullptr);
    QString out;
    for (USHORT i = 0; i < count; ++i) {
        QString name;
        quint64 rva = 0;
        if (!moduleOfAddress(frames[i], &name, &rva))
            continue;
        out += QStringLiteral("    | #%1 %2\n").arg(i).arg(formatFrame(name, rva));
    }
    return out;
#else
    Q_UNUSED(skipFrames);
    return QString();
#endif
}

// ---------------------------------------------------------------------------
// 安装 / 落盘
// ---------------------------------------------------------------------------

void install(const QString &dataDir, qint64 maxBytes)
{
    if (g.installed)
        return;

    const QString dir = QDir(dataDir).filePath(QStringLiteral("logs"));
    QDir().mkpath(dir);
    g.logPath = QDir(dir).filePath(QStringLiteral("adoloop.log"));
    g.terminatePath = QDir(dir).filePath(QStringLiteral("terminate.log"));
    g.limit = maxBytes > 0 ? maxBytes : (2 * 1024 * 1024);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const QFileInfo fi(g.logPath);
        g.bytes = fi.exists() ? fi.size() : 0;
    }
    g.installed = true;

    // 先挂处理器再写横幅：横幅本身也走同一条链路，顺便验证链路是通的
    g.prev = qInstallMessageHandler(messageHandler);
    std::set_terminate(terminateHandler);

    emitBanner();
    runSelfTestIfRequested();
}

void logExit(int exitCode)
{
    if (!g.installed)
        return;
    Record r;
    r.epochMs = QDateTime::currentMSecsSinceEpoch();
    r.pid = currentPid();
    r.level = QtInfoMsg;
    r.category = QStringLiteral("app");
    r.message = QStringLiteral("AdoLoop 退出：code=%1（有「启动」无「退出」= 该次运行非正常结束）")
                    .arg(exitCode);
    emitRecord(r);
}

void uninstall()
{
    if (!g.installed)
        return;
    qInstallMessageHandler(g.prev);
    g.prev = nullptr;
    g.installed = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        closeLocked();
    }
}

QString logFilePath() { return g.logPath; }
QString terminateFilePath() { return g.terminatePath; }

} // namespace adoloop::crashlog
