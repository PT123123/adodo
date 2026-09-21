#pragma once

#include "../core/Types.h"

#include <QString>
#include <QVector>

namespace adoloop {

// 语音特征分析（跟读比对/声控触发）：不依赖第三方库，纯启发式。
// - VAD：帧 RMS 阈值 → 语音起点（声控跟读触发）
// - 比对：模型音与跟读音的 RMS 包络 DTW/相关 → 0..100 分
class AudioAnalysis {
public:
    // 读取 WAV（16bit PCM 单/双声道）→ 归一化浮点（mono 混合）；失败返回空
    static QVector<float> readWavMono(const QString &wavPath, int *sampleRateOut = nullptr);

    // 帧 RMS 包络（25ms 帧、10ms 步进），返回每帧 RMS（0..1）
    static QVector<float> rmsEnvelope(const QVector<float> &pcm, int sampleRate);

    // 语音起点：首个超过阈值的语音帧时间（ms）；无人声返回 -1
    static Ms voiceOnsetMs(const QVector<float> &pcm, int sampleRate, double threshold = 0.05);

    // 跟读比对：模型音 vs 用户音 → 得分
    static ShadowingScore compare(const QVector<float> &modelPcm, int modelSr,
                                  const QVector<float> &userPcm, int userSr);

    // 把包络重采样到固定点数（长度归一化）
    static QVector<float> resample(const QVector<float> &in, int targetN);
};

} // namespace adoloop
