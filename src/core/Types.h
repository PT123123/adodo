#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace adoloop {

// 时间统一用毫秒（qint64）
using Ms = qint64;

// 学习语言（M11）：决定分词、词典节点解析与发音音频的走向。
// 默认日语（用户实际学习语言）；英语路径行为与 M10 前完全一致。
enum class Lang { English, Japanese };

enum class RepeatMode { Sentence, AbLoop, FullScript };
enum class ShadowingMode { Manual, VoiceTriggered, Crazy };
enum class DictationMode { Word, Sentence, Free };

enum class EngineStatus { Idle, Queued, Running, Finished, Failed, Cancelled };

// 词级时间戳（ASR 权威来源；缺失时由 Sentence::estimatedWords() 插值）
struct WordToken {
    QString word;
    Ms start = 0;
    Ms end = 0;
};

// 一个学习句（来自字幕、ASR 或断句）的完整定义见 core/Sentence.h
// （此处仅前置声明，避免与 Sentence.h 中的类定义重复）
class Sentence;

// ASR 引擎输出的一段
struct AsrSegment {
    Ms start = 0;
    Ms end = 0;
    QString text;
    QVector<WordToken> words;
};

// 时间区间（断句/复读 A-B）
struct TimeRange {
    Ms start = 0;
    Ms end = 0;
    bool valid() const { return end > start; }
    Ms duration() const { return end - start; }
};

struct MediaInfo {
    QString pathOrUrl;     // 本地路径或解析后的流地址
    QString title;
    qint64 durationMs = 0;
    bool isOnline = false; // 来自 yt-dlp 解析的在线媒体
};

// 日语化读音信息（M12）：生词本/闪卡/弹幕/字幕悬停共用的四个字段。
// 来源：在线查词（权威）或分词引擎（MeCab 档）；内置启发式分词给不出读音。
struct ReadingInfo {
    QString reading; // 假名读音（日语）
    QString accent;  // 声调（如 "②"；平板为 "⓪"）
    QString pos;     // 词性（如 "名词"）
    QString lang;    // 语言代码 "ja" / "en"
    bool empty() const { return reading.isEmpty() && accent.isEmpty() && pos.isEmpty(); }
};

// 词典条目（在线查词结果，简单结构）
// 日语（M11）：word=表记、reading=假名、accent=声调（如 "②"）、pos=词性、audioUrl=发音；
// 英语：word=词条、phoneticUs/Uk=音标、audioUs/Uk=美/英音；audioUrl 为统一入口（朗读按钮用）。
struct DictEntry {
    QString word;
    QString reading;       // 日语假名读音（英语留空）
    QString accent;        // 日语声调（英语留空）
    QString pos;           // 日语词性（M12：trs[].pos；英语路径可能为空）
    QString phoneticUs;    // 美音音标
    QString phoneticUk;    // 英音音标
    QStringList definitions; // 释义列表
    QStringList examples;     // 例句
    QString audioUs;          // 美音音频 URL（可空）
    QString audioUk;          // 英音音频 URL（可空）
    QString audioUrl;         // 统一发音音频 URL（M11 朗读按钮使用，可空）
    Lang lang = Lang::Japanese; // 结果所属语言（决定弹窗标题排版）
    bool ok() const { return !definitions.isEmpty(); }
};

// 智能断句参数（Aboboo 五参数）
struct SegmentParams {
    double noiseThreshold = 0.02; // 背景噪音阈值（RMS 归一化 0..1）
    int minGapMs = 280;           // 句间停顿（毫秒）
    int minSentenceMs = 400;      // 最短句长（毫秒），更短并入邻句
    int allowedNoiseMs = 120;     // 允许杂音数：短于此的停顿不切分
    bool removeSilence = false;   // 静音去除（波形显示压缩，v1 仅保留字段）
};

// 跟读比对得分（0..100）
struct ShadowingScore {
    int score = 0;
    double coverage = 0.0;   // 覆盖度
    double rhythm = 0.0;     // 节奏相似度
    QString detail;
};

} // namespace adoloop
