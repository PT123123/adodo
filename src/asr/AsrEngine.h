#pragma once

#include "../core/Types.h"

#include <QObject>
#include <QString>
#include <QVector>

namespace adoloop {

// ASR 引擎抽象。实现：WhisperCppAsr（whisper.cpp CLI）、FasterWhisperAsr（Python 桥）。
// 结果统一为 QVector<AsrSegment>（含词级时间戳）。
class AsrEngine : public QObject {
    Q_OBJECT
public:
    enum class Engine { WhisperCpp, FasterWhisper };
    Q_ENUM(Engine)

    struct Options {
        QString model;    // whisper.cpp: 模型路径；faster-whisper: 模型名/路径
        QString language; // "en" / "auto"
        QString outputDir;
        int beamSize = 5;
    };

    explicit AsrEngine(QObject *parent = nullptr) : QObject(parent) {}

    virtual Engine engine() const = 0;
    virtual QString engineName() const = 0;
    virtual bool isAvailable(QString *reason = nullptr) const = 0;

    // 异步开始；进度/结果/错误经信号回传
    virtual void start(const QString &mediaPath, const Options &opt) = 0;
    virtual void cancel() = 0;
    virtual bool isRunning() const = 0;

signals:
    void progress(float fraction, const QString &stage);
    void finished(const QVector<adoloop::AsrSegment> &segments);
    void failed(const QString &error);
    void cancelled();
};

} // namespace adoloop
