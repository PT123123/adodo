#pragma once

#include "../core/Types.h"

#include <QObject>
#include <QProcess>
#include <QVector>

namespace adoloop {

// 波形数据：外部 ffmpeg 解码为 8kHz 单声道 PCM，按 10ms 块计算 min/max 峰值，
// 缓存到 <cacheDir>/<sha1(mediaPath)>.wvpk。Widget 按像素列聚合显示。
class Waveform : public QObject {
    Q_OBJECT
public:
    explicit Waveform(QObject *parent = nullptr);

    // 异步生成波形（不阻塞 UI）。path 不存在/无 ffmpeg 时发 failed。
    void generate(const QString &mediaPath, const QString &cacheDir);

    // 同步加载缓存；成功返回 true
    bool loadCache(const QString &cachePath);

    const QVector<float> &minPeaks() const { return m_min; }
    const QVector<float> &maxPeaks() const { return m_max; }
    int peakCount() const { return m_min.size(); }
    bool isReady() const { return !m_min.isEmpty(); }
    Ms totalDurationMs() const { return m_totalMs; }
    // 10ms 块大小（固定）
    static constexpr Ms kBlockMs = 10;

private:
    void runFfmpeg(const QString &mediaPath, const QString &cachePath);
    void processPcm(const QByteArray &pcm, Ms totalMs);
    void finish();

    QProcess m_proc;
    QByteArray m_pcm;
    QVector<float> m_min;
    QVector<float> m_max;
    Ms m_totalMs = 0;
    bool m_running = false;

signals:
    void ready();
    void failed(const QString &error);
    void progress(float fraction);
};

} // namespace adoloop
