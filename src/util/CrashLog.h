#pragma once

// 崩溃可观测性（M15）：把 Qt 消息与 terminate 标记落盘，供事后定位启动期 abort。
//
// 为什么需要它：
//   * 本程序按 Windows GUI 子系统链接（WIN32_EXECUTABLE），stderr 在多数启动方式下拿不到；
//   * 没有消息处理器时，qFatal / Q_ASSERT 只打印到 stderr 然后立刻 abort，
//     证据随进程一起消失（WER 只留下「Qt6Core.dll + qAbort 的偏移」，看不出是谁喊的）。
//
// 设计约束：
//   * 只做可观测性，不碰业务逻辑；任何 Qt 消息都原样转发给原处理器（保持 stderr 诊断不退化）；
//   * warning 及以上立即 flush —— abort 不会冲刷用户态缓冲，缓冲过的行等于没写；
//   * 单文件超过上限即滚动（adoloop.log → adoloop.log.1，只留一份历史），不会无限增长；
//   * 落盘用 C stdio（_wfopen/fwrite）而不是 QFile：崩溃路径上 Qt 内部状态可能已不可靠，
//     且这里要精确控制 flush 时机。
//
// 可单测性：Record 是「一条日志」的归一化形式，formatLine / shouldRotate / terminateText
// 都是纯函数（不碰文件、不碰全局状态），可离线验证格式化与滚动/致命标记是否正确。

#include <QString>
#include <QtGlobal>

namespace adoloop::crashlog {

// 一条已归一化的日志记录（纯数据）
struct Record {
    qint64  epochMs = 0;      // 发生时刻（毫秒，本地时区显示）
    qint64  pid = 0;          // 进程号：并发跑多个实例时用来区分归属
    int     level = 1;        // QtMsgType（0=debug 1=warning 2=critical 3=fatal 4=info）
    QString category;         // Qt 日志类别（可空）
    QString file;             // 源文件（可空）
    int     line = 0;         // 行号（file 为空时忽略）
    QString message;          // 正文（多行会被折成缩进续行，一条记录只占一行）
    bool    fatal = false;    // 致命级别：写入后立即 flush，并另写一份 terminate.log
};

// 级别数值 → 名称（debug/info/warning/critical/fatal）
QString levelName(int level);

// Record → 一行文本（含结尾换行）。纯函数，时间只由 epochMs 决定。
QString formatLine(const Record &r);

// 滚动判定：当前大小 + 本次写入是否会超过上限（纯函数；limitBytes<=0 表示不滚动）
bool shouldRotate(qint64 currentBytes, qint64 incomingBytes, qint64 limitBytes);

// terminate 标记正文（纯函数）。hadException=false 表示没有活动异常
// （noexcept 违规 / 纯虚调用 / 直接调用 std::terminate）。
QString terminateText(const QString &exceptionType, bool hadException);

// 栈帧文本（纯函数）：模块名 + 模块内偏移，形如 "Qt6Gui.dll+0x1a2b3c"。
// 为什么记 module+RVA 而不是函数名：本机没有调试器/符号，也不下载任何外部工具；
// 但 module+RVA 可以和「已知原因的对照实验」逐帧比对（指纹比对），从而区分是哪条路径。
QString formatFrame(const QString &moduleName, quint64 rva);

// 采集当前调用栈（module+RVA 文本，每条一行、已缩进；失败返回空串）。
// 只在致命级别/terminate 时调用（CaptureStackBackTrace + VirtualQuery，无分配、不依赖符号）。
QString captureStackText(int skipFrames = 0);

// 安装 Qt 消息处理器 + std::terminate 处理器；日志写到 <dataDir>\logs\adoloop.log。
// 幂等；会先建 logs 目录，并写一条启动横幅（版本/pid/平台/环境变量等定位信息）。
void install(const QString &dataDir, qint64 maxBytes = 2 * 1024 * 1024);

// 正常退出标记（与启动横幅配对：有启动无退出 = 该次运行非正常结束）
void logExit(int exitCode);

// 恢复安装前的消息处理器并关闭日志文件（自检程序用；产品里不需要调用）
void uninstall();

QString logFilePath();
QString terminateFilePath();

} // namespace adoloop::crashlog
