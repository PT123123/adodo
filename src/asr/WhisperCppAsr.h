#pragma once

#include "AsrEngine.h"

#include <QProcess>

namespace adoloop {

// whisper.cpp CLI 适配：`whisper-cli -m model -l lang -f media -oj -of base -s`
// 解析输出的 <base>.json（transcription[].offsets / words[]）。
class WhisperCppAsr : public AsrEngine {
    Q_OBJECT
public:
    explicit WhisperCppAsr(QObject *parent = nullptr);

    Engine engine() const override { return Engine::WhisperCpp; }
    QString engineName() const override { return QStringLiteral("whisper.cpp"); }
    bool isAvailable(QString *reason = nullptr) const override;
    void start(const QString &mediaPath, const Options &opt) override;
    void cancel() override;
    bool isRunning() const override { return m_proc.state() != QProcess::NotRunning; }

private:
    void onFinished(int exitCode);
    void parseJson(const QString &jsonPath);

    QProcess m_proc;
    QString m_outputBase;
};

} // namespace adoloop
