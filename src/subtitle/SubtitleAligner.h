#pragma once

#include "../core/Sentence.h"
#include "../core/Types.h"

#include <QString>
#include <QVector>

namespace adoloop {

// 智能断句 + ASR 结果转句 + 与参照字幕对齐。
// 全部为纯静态函数，便于单元测试。
class SubtitleAligner {
public:
    // Aboboo 式智能断句：输入 8kHz 单声道 PCM + 五参数 → 句段时间区间。
    // 能量包络阈值检测（背景噪音）→ 长停顿切分（句间停顿）→ 短句合并（最短句长）
    // → 抑制杂音毛刺（允许杂音数）。
    static QVector<TimeRange> segmentBySilence(const QVector<float> &pcm8k,
                                               const SegmentParams &params);

    // ASR 段 → 学习句：按停顿 gap 合并；词级时间戳保留。
    // 文本清洗：去除句首尾空格，保留内部标点。
    static QVector<Sentence> sentencesFromAsr(const QVector<AsrSegment> &segments,
                                              int maxGapMs = 600);

    // 把 ASR 句对齐到参照字幕：按时间最近 + 文本相似度匹配，
    // 返回匹配上的句子数；匹配结果写回 fromAsr[i].text/translation（v1 只做时间标注）。
    // 说明：完整「字幕时间规整」属进阶功能，v1 提供基础最近邻匹配。
    static int alignToReference(QVector<Sentence> &fromAsr,
                                const QVector<Sentence> &reference);

    // 文本相似度 0..1（归一化编辑距离），对齐用
    static double textSimilarity(const QString &a, const QString &b);
};

} // namespace adoloop
