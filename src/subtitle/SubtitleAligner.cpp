#include "SubtitleAligner.h"
#include "../util/StringUtil.h"

#include <QtMath>

namespace adoloop {

QVector<TimeRange> SubtitleAligner::segmentBySilence(const QVector<float> &pcm8k,
                                                     const SegmentParams &params)
{
    QVector<TimeRange> out;
    if (pcm8k.isEmpty())
        return out;

    constexpr int kSampleRate = 8000;
    constexpr int kFrameLen = 160;   // 20ms
    constexpr int kFrameHop = 80;    // 10ms
    constexpr int kFrameMs = 20;

    // 帧 RMS
    QVector<float> frameRms;
    for (int off = 0; off + kFrameLen <= pcm8k.size(); off += kFrameHop) {
        double acc = 0.0;
        for (int i = off; i < off + kFrameLen; ++i)
            acc += double(pcm8k[i]) * double(pcm8k[i]);
        frameRms.append(float(qSqrt(acc / kFrameLen)));
    }
    if (frameRms.isEmpty())
        return out;

    // 背景噪音阈值：参数值按 0..1 归一化 RMS 解释
    const double thr = qBound(0.001, params.noiseThreshold, 0.5);

    // 有声/无声标记（每帧 20ms）
    const int n = frameRms.size();
    QVector<bool> voice(n);
    for (int i = 0; i < n; ++i)
        voice[i] = frameRms[i] > thr;

    // 抑制杂音毛刺：短于 allowedNoiseMs 的孤立状态翻转被抹平
    const int minRun = qMax(2, params.allowedNoiseMs / kFrameMs);
    for (int i = 1; i < n - 1; ++i) {
        if (voice[i] != voice[i - 1]) {
            int run = 1;
            int j = i;
            while (j + 1 < n && voice[j + 1] == voice[i]) {
                ++run;
                ++j;
            }
            if (run <= minRun && i > 0 && j + 1 < n) {
                for (int k = i; k <= j; ++k)
                    voice[k] = voice[i - 1];
            }
            i = j;
        }
    }

    // 候选断点：无声段长度 >= minGapMs
    QVector<int> cuts;
    int silenceStart = -1;
    for (int i = 0; i <= n; ++i) {
        const bool v = (i < n) ? voice[i] : false;
        if (!v && silenceStart < 0)
            silenceStart = i;
        else if (v && silenceStart >= 0) {
            const int silenceLenMs = (i - silenceStart) * kFrameMs;
            if (silenceLenMs >= params.minGapMs)
                cuts << silenceStart * kFrameMs;
            silenceStart = -1;
        }
    }

    if (cuts.isEmpty()) {
        out << TimeRange{0, Ms(pcm8k.size()) * 1000 / kSampleRate};
        return out;
    }

    // 组句 + 最短句长合并
    Ms segStart = 0;
    for (int c = 0; c <= cuts.size(); ++c) {
        const Ms segEnd = (c < cuts.size()) ? Ms(cuts[c]) : Ms(pcm8k.size()) * 1000 / kSampleRate;
        if (segEnd - segStart >= params.minSentenceMs) {
            out << TimeRange{segStart, segEnd};
            segStart = segEnd;
        } else {
            // 并入下一段：不落断点（继续延伸当前段）
        }
    }
    if (!out.isEmpty() && segStart < Ms(pcm8k.size()) * 1000 / kSampleRate) {
        const Ms end = Ms(pcm8k.size()) * 1000 / kSampleRate;
        out.last().end = end;
    }
    if (out.isEmpty())
        out << TimeRange{0, Ms(pcm8k.size()) * 1000 / kSampleRate};

    return out;
}

QVector<Sentence> SubtitleAligner::sentencesFromAsr(const QVector<AsrSegment> &segments,
                                                    int maxGapMs)
{
    QVector<Sentence> out;
    if (segments.isEmpty())
        return out;

    Sentence cur;
    cur.id = 0;
    for (int i = 0; i < segments.size(); ++i) {
        const AsrSegment &seg = segments[i];
        if (!cur.valid()) {
            cur.start = seg.start;
            cur.end = seg.end;
            cur.text = seg.text.trimmed();
            cur.words = seg.words;
            continue;
        }
        const Ms gap = seg.start - cur.end;
        if (gap >= 0 && gap <= maxGapMs && cur.duration() < 12000) {
            // 合并
            cur.end = qMax(cur.end, seg.end);
            cur.text = (cur.text + ' ' + seg.text.trimmed()).trimmed();
            cur.words += seg.words;
        } else {
            if (!cur.text.isEmpty())
                out << cur;
            cur = Sentence{};
            cur.id = int(out.size());
            cur.start = seg.start;
            cur.end = seg.end;
            cur.text = seg.text.trimmed();
            cur.words = seg.words;
        }
    }
    if (cur.valid() && !cur.text.isEmpty())
        out << cur;

    for (int i = 0; i < out.size(); ++i)
        out[i].id = i;
    return out;
}

double SubtitleAligner::textSimilarity(const QString &a, const QString &b)
{
    const QString na = strutil::normalizeForCheck(a, true, true);
    const QString nb = strutil::normalizeForCheck(b, true, true);
    if (na.isEmpty() || nb.isEmpty())
        return 0.0;
    const int d = strutil::levenshtein(na, nb);
    const int maxLen = qMax(na.size(), nb.size());
    return 1.0 - double(d) / double(maxLen);
}

int SubtitleAligner::alignToReference(QVector<Sentence> &fromAsr,
                                      const QVector<Sentence> &reference)
{
    if (reference.isEmpty())
        return 0;
    int matched = 0;
    for (Sentence &s : fromAsr) {
        // 找时间重叠最多的参照句
        int bestIdx = -1;
        Ms bestOverlap = 0;
        double bestSim = 0.0;
        for (int i = 0; i < reference.size(); ++i) {
            const Sentence &r = reference[i];
            const Ms overlap = qMin(s.end, r.end) - qMax(s.start, r.start);
            if (overlap <= 0)
                continue;
            const double sim = textSimilarity(s.text, r.text);
            if (overlap > bestOverlap || (overlap == bestOverlap && sim > bestSim)) {
                bestOverlap = overlap;
                bestIdx = i;
                bestSim = sim;
            }
        }
        if (bestIdx >= 0) {
            // 用参照文本修正 ASR 文本（仅当相似度足够高，避免误改）
            if (bestSim >= 0.6)
                s.text = reference[bestIdx].text;
            s.translation = reference[bestIdx].translation;
            ++matched;
        }
    }
    return matched;
}

} // namespace adoloop
