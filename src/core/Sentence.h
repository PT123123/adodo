#pragma once

#include "Tokenizer.h"
#include "Types.h"

namespace adoloop {

// 一个学习句：文本 + 时间 + 词级时间戳 + 意群。
class Sentence {
public:
    int id = -1;
    Ms start = 0;
    Ms end = 0;
    QString text;
    QString translation;
    QVector<WordToken> words;
    // 分词结果（M11）：非空时优先使用（外部引擎档带读音/原形）；
    // 为空则按 lang 现场走内置启发式分词。
    QVector<Token> tokens;
    // 学习语言：由 app 层经 SubtitleModel 注入（core 层不依赖 app/Settings）
    Lang lang = Lang::Japanese;

    bool valid() const { return end > start && !text.isEmpty(); }
    Ms duration() const { return end - start; }
    bool japanese() const { return lang == Lang::Japanese; }

    // 语言感知分词（tokens 非空则直接返回；否则按 lang 现场分词）
    QVector<Token> lexTokens() const;

    // 纯词表（去标点），抠词听写/生词本用
    QStringList wordList() const;

    // 词级时间戳：优先 words；缺失则按字符占比在句内插值（估计值）
    QVector<WordToken> estimatedWords() const;

    // 意群划分（Echo-Loop）：按标点/长停顿切分；无词时间戳时按标点切分
    QVector<TimeRange> chunks(int maxChunks = 4) const;

    // 挖空文本（闪卡）：ratio 比例的词被替换为 ____
    QString clozeText(double ratio = 0.3) const;

    // 取句中某词的区间（找不到返回 {-1,-1}）
    TimeRange wordRange(const QString &word, int occurrence = 0) const;

    static Sentence fromWords(const QString &text, Ms start, Ms end,
                              const QVector<WordToken> &tokens);
};

} // namespace adoloop
