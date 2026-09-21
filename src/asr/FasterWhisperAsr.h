#pragma once

#include "AsrEngine.h"

#include <QProcess>

namespace adoloop {

// faster-whisper 适配：运行 scripts/faster_whisper_bridge.py（Python 桥）。
// 桥协议（stdout 逐行 JSON）：
//   {"progress": 0.35}
//   {"result": [ {start,end,text,words:[{word,start,end}]} ]}
class FasterWhisperAsr : public AsrEngine {
    Q_OBJECT
public:
    explicit FasterWhisperAsr(QObject *parent = nullptr);

    Engine engine() const override { return Engine::FasterWhisper; }
    QString engineName() const override { return QStringLiteral("faster-whisper"); }
    bool isAvailable(QString *reason = nullptr) const override;
    void start(const QString &mediaPath, const Options &opt) override;
    void cancel() override;
    bool isRunning() const override { return m_proc.state() != QProcess::NotRunning; }

    // 桥脚本路径（随应用分发，位于 <可执行目录>/scripts/ 或源码树）
    static QString bridgeScriptPath();

private:
    void onLine(const QByteArray &line);
    void onFinished(int exitCode);

    QProcess m_proc;
};

} // namespace adoloop
