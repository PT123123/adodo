#include "AudioAnalysis.h"

#include <QFile>
#include <QtMath>

namespace adoloop {

QVector<float> AudioAnalysis::readWavMono(const QString &wavPath, int *sampleRateOut)
{
    QVector<float> out;
    QFile f(wavPath);
    if (!f.open(QIODevice::ReadOnly))
        return out;

    const QByteArray data = f.readAll();
    f.close();
    if (data.size() < 44 || memcmp(data.constData(), "RIFF", 4) != 0)
        return out;

    // 定位 data chunk（考虑 fmt 等块）
    int dataOff = -1;
    int dataLen = 0;
    int sampleRate = 0;
    int channels = 1;
    int bits = 16;
    int pos = 12;
    while (pos + 8 <= data.size()) {
        const QByteArray id = data.mid(pos, 4);
        quint32 sz = 0;
        memcpy(&sz, data.constData() + pos + 4, 4);
        if (id == "fmt ") {
            memcpy(&channels, data.constData() + pos + 10, 2);
            memcpy(&sampleRate, data.constData() + pos + 12, 4);
            memcpy(&bits, data.constData() + pos + 22, 2);
        } else if (id == "data") {
            dataOff = pos + 8;
            dataLen = int(sz);
            break;
        }
        pos += 8 + int(sz) + (sz & 1); // 块按 2 字节对齐
    }
    if (dataOff < 0 || sampleRate <= 0 || (bits != 16 && bits != 8))
        return out;
    if (sampleRateOut)
        *sampleRateOut = sampleRate;

    const int totalSamples = qMin(dataLen, data.size() - dataOff);
    const int bytesPerSample = bits / 8;
    const int n = totalSamples / bytesPerSample / qMax(1, channels);
    out.reserve(n);

    const char *p = data.constData() + dataOff;
    if (bits == 16) {
        for (int i = 0; i < n; ++i) {
            qint16 sum = 0;
            for (int c = 0; c < channels; ++c) {
                qint16 s;
                memcpy(&s, p + (i * channels + c) * 2, 2);
                sum += s;
            }
            out.append(float(sum) / (32768.0f * channels));
        }
    } else { // 8bit unsigned
        for (int i = 0; i < n; ++i) {
            qint32 sum = 0;
            for (int c = 0; c < channels; ++c) {
                const quint8 s = quint8(p[(i * channels + c)]);
                sum += int(s) - 128;
            }
            out.append(float(sum) / (128.0f * channels));
        }
    }
    return out;
}

QVector<float> AudioAnalysis::rmsEnvelope(const QVector<float> &pcm, int sampleRate)
{
    QVector<float> out;
    if (pcm.isEmpty() || sampleRate <= 0)
        return out;
    const int frameLen = qMax(1, sampleRate / 40);   // 25ms
    const int hop = qMax(1, sampleRate / 100);       // 10ms
    for (int off = 0; off + frameLen <= pcm.size(); off += hop) {
        double acc = 0.0;
        for (int i = off; i < off + frameLen; ++i)
            acc += double(pcm[i]) * double(pcm[i]);
        out.append(float(qSqrt(acc / frameLen)));
    }
    return out;
}

Ms AudioAnalysis::voiceOnsetMs(const QVector<float> &pcm, int sampleRate, double threshold)
{
    if (sampleRate <= 0)
        return -1;
    const QVector<float> env = rmsEnvelope(pcm, sampleRate);
    if (env.isEmpty())
        return -1;

    double sum = 0.0;
    for (float v : env)
        sum += v;
    const double meanRms = sum / env.size();
    const double thr = qMax(threshold, meanRms * 0.35);

    // 至少连续 3 帧超过阈值才算语音
    int streak = 0;
    const int hopSamples = qMax(1, sampleRate / 100);
    for (int i = 0; i < env.size(); ++i) {
        if (env[i] > thr) {
            ++streak;
            if (streak >= 3)
                return Ms(i * hopSamples * 1000 / sampleRate);
        } else {
            streak = 0;
        }
    }
    return -1;
}

QVector<float> AudioAnalysis::resample(const QVector<float> &in, int targetN)
{
    QVector<float> out;
    if (in.isEmpty() || targetN <= 0)
        return out;
    out.reserve(targetN);
    for (int i = 0; i < targetN; ++i) {
        const double t = double(i) * (in.size() - 1) / double(targetN - 1);
        const int i0 = int(t);
        const int i1 = qMin(i0 + 1, in.size() - 1);
        const float frac = float(t - i0);
        out.append(in[i0] * (1.0f - frac) + in[i1] * frac);
    }
    return out;
}

ShadowingScore AudioAnalysis::compare(const QVector<float> &modelPcm, int modelSr,
                                      const QVector<float> &userPcm, int userSr)
{
    ShadowingScore score;
    if (modelPcm.isEmpty() || userPcm.isEmpty() || modelSr <= 0 || userSr <= 0)
        return score;

    const QVector<float> mEnv = rmsEnvelope(modelPcm, modelSr);
    const QVector<float> uEnv = rmsEnvelope(userPcm, userSr);
    if (mEnv.isEmpty() || uEnv.isEmpty())
        return score;

    const int N = 100;
    const QVector<float> m = resample(mEnv, N);
    const QVector<float> u = resample(uEnv, N);

    // 覆盖度：用户有声帧占比
    double userVoice = 0.0;
    for (float v : u)
        if (v > 0.02)
            userVoice += 1.0;
    score.coverage = userVoice / N;

    // 节奏相似度：Pearson 相关
    double mm = 0.0, um = 0.0;
    for (int i = 0; i < N; ++i) {
        mm += m[i];
        um += u[i];
    }
    mm /= N;
    um /= N;
    double cov = 0.0, vm = 0.0, vu = 0.0;
    for (int i = 0; i < N; ++i) {
        const double dm = m[i] - mm;
        const double du = u[i] - um;
        cov += dm * du;
        vm += dm * dm;
        vu += du * du;
    }
    score.rhythm = (vm > 1e-9 && vu > 1e-9) ? cov / qSqrt(vm * vu) : 0.0;
    score.rhythm = qBound(-1.0, score.rhythm, 1.0);

    // 幅度比：跟读整体音量相对模型的比例（限制在 0.5..2 之间）
    double mPow = 0.0, uPow = 0.0;
    for (float v : m)
        mPow += v;
    for (float v : u)
        uPow += v;
    const double ratio = (mPow > 1e-9) ? uPow / mPow : 0.0;

    // 综合分：覆盖度 30% + 节奏 50% + 幅度贴近 20%
    double score01 = 0.30 * qMin(1.0, score.coverage)
        + 0.50 * qMax(0.0, score.rhythm)
        + 0.20 * qMax(0.0, 1.0 - qAbs(qLn(qMax(0.05, ratio))));
    score.score = int(qRound(qBound(0.0, score01, 1.0) * 100.0));
    score.detail = QStringLiteral("覆盖 %.0f%%，节奏相似 %.0f%%")
                       .arg(score.coverage * 100.0)
                       .arg(qMax(0.0, score.rhythm) * 100.0);
    return score;
}

} // namespace adoloop
