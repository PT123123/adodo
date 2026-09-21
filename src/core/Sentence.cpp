#include "Sentence.h"
#include "../util/StringUtil.h"
#include "../util/TimeUtil.h"

#include <QRegularExpression>
#include <QRandomGenerator>
#include <QSet>

namespace adoloop {

QVector<Token> Sentence::lexTokens() const
{
    if (!tokens.isEmpty())
        return tokens;
    return Tokenizer::tokens(text, lang);
}

QStringList Sentence::wordList() const
{
    QStringList words;
    for (const Token &t : lexTokens())
        words << t.surface;
    return words;
}

QVector<WordToken> Sentence::estimatedWords() const
{
    if (!words.isEmpty())
        return words;
    if (!valid())
        return {};

    QVector<WordToken> out;
    const QStringList ws = wordList();
    if (ws.isEmpty())
        return out;

    // 按字符数占比在句内插值
    int totalChars = 0;
    for (const QString &w : ws)
        totalChars += w.size();
    if (totalChars <= 0)
        return out;

    const Ms dur = duration();
    Ms cursor = start;
    for (int i = 0; i < ws.size(); ++i) {
        // 末词吃掉整数取整的余量，保证估计区间铺满整句（M11）
        const Ms wlen = (i == ws.size() - 1)
            ? qMax<Ms>(1, start + dur - cursor)
            : Ms(dur) * Ms(ws[i].size()) / Ms(totalChars);
        WordToken t{ws[i], cursor, cursor + wlen};
        out << t;
        cursor += wlen;
    }
    return out;
}

QVector<TimeRange> Sentence::chunks(int maxChunks) const
{
    QVector<TimeRange> out;
    const QVector<WordToken> ws = estimatedWords();
    if (ws.isEmpty())
        return out;

    // 用词时间戳切意群：遇到「, ; : — 停顿 gap > 350ms」切开
    static const QString kBoundary = QStringLiteral(",:;—…。！？，、；：");

    // 遍历词，判断前一词文本尾部是否含标点，或 gap 过大
    Ms chunkStart = ws.first().start;
    Ms lastEnd = ws.first().end;
    int count = 0;
    for (int i = 1; i < ws.size(); ++i) {
        const QString &prevWord = ws[i - 1].word;
        const WordToken &cur = ws[i];
        const bool punctBoundary = !prevWord.isEmpty() && kBoundary.contains(prevWord.back());
        const bool longGap = (cur.start - lastEnd) > 350 && out.size() < maxChunks;
        if ((punctBoundary || longGap) && count >= 1 && out.size() < maxChunks - 1) {
            out << TimeRange{chunkStart, cur.start};
            chunkStart = cur.start;
            count = 0;
        }
        lastEnd = cur.end;
        ++count;
    }
    if (lastEnd > chunkStart)
        out << TimeRange{chunkStart, lastEnd};
    if (out.isEmpty() && ws.size() > 0)
        out << TimeRange{ws.first().start, ws.last().end};
    return out;
}

QString Sentence::clozeText(double ratio) const
{
    const QStringList ws = wordList();
    if (ws.isEmpty())
        return text;

    const int count = qMax(1, int(ws.size() * ratio));
    // 确定性伪随机：按 id 做种子，保证同一句每次挖空一致
    QRandomGenerator rng(quint32(qMax(0, id)));
    QSet<int> chosen;
    while (chosen.size() < count && chosen.size() < ws.size())
        chosen.insert(int(rng.bounded(quint32(ws.size()))));

    QStringList out;
    for (int i = 0; i < ws.size(); ++i) {
        if (chosen.contains(i))
            out << QStringLiteral("____");
        else
            out << ws[i];
    }
    return out.join(japanese() ? QString() : QStringLiteral(" "));
}

TimeRange Sentence::wordRange(const QString &word, int occurrence) const
{
    const QVector<WordToken> ws = estimatedWords();
    const QString target = word.trimmed();
    int seen = 0;
    for (const WordToken &t : ws) {
        if (t.word.compare(target, Qt::CaseInsensitive) == 0) {
            if (seen == occurrence)
                return TimeRange{t.start, t.end};
            ++seen;
        }
    }
    return TimeRange{-1, -1};
}

Sentence Sentence::fromWords(const QString &text, Ms start, Ms end, const QVector<WordToken> &tokens)
{
    Sentence s;
    s.text = text;
    s.start = start;
    s.end = end;
    s.words = tokens;
    return s;
}

} // namespace adoloop
