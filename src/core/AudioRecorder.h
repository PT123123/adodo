#pragma once

#include "../core/Types.h"

#include <QAudioSource>
#include <QFile>
#include <QIODevice>
#include <QObject>

namespace adoloop {

// 录音（跟读/听音查字用）：16kHz 单声道 16bit PCM → WAV 文件 或 内存缓冲。
// start()：写文件模式；startToBuffer()：内存缓冲模式（声控跟读 VAD 需要）。
class AudioRecorder : public QObject {
    Q_OBJECT
public:
    explicit AudioRecorder(QObject *parent = nullptr);
    ~AudioRecorder() override;

    // 是否有可用的录音输入设备（M12：段落复述据此决定「录音」还是「静默回忆」降级）
    static bool inputAvailable();

    bool isRecording() const { return m_recording; }
    bool isBuffered() const { return m_buffered; }

    // 文件模式
    bool start(const QString &destWavPath);
    void stop();

    // 缓冲模式：录音到内存，drainBuffer() 取走数据
    bool startToBuffer();
    // 停止并返回已录制的 PCM（16bit 单声道，按当前格式）
    QByteArray stopToBuffer();
    // 取走并清空缓冲（不停止录音）
    QByteArray drainBuffer();
    qint64 bufferedBytes() const { return m_buffer.size(); }

    QString filePath() const { return m_filePath; }
    // 录音毫秒（按采样数估算）
    Ms recordedMs() const;
    bool hasData() const { return m_bytes > 0 || !m_buffer.isEmpty(); }
    int sampleRate() const { return m_source.format().sampleRate(); }

signals:
    void started();
    void stopped(adoloop::Ms recordedMs);
    void failed(const QString &error);

private:
    QAudioSource m_source;
    QFile m_file;
    QString m_filePath;
    QByteArray m_buffer;
    QIODevice *m_bufferDevice = nullptr; // 缓冲模式写入设备（本类所有）
    bool m_recording = false;
    bool m_buffered = false;
    qint64 m_bytes = 0; // 文件模式下写入的数据字节
};

} // namespace adoloop
