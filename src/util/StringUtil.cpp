#include "StringUtil.h"

#include <QRegularExpression>

namespace adoloop::strutil {

namespace {
// 半角 + 全角 + 日文标点（M11 补全：日本語の約物与全角 ASCII 变体）
const QString kPunct = QStringLiteral(
    ".,;:!?…\"'“”‘’()[]{}<>《》【】「」『』、，。；：！？—…·…"
    "・･〜～〈〉《》「」『』【】〔〕〖〗〘〙〚〛｡｢｣､"          // 日文约物
    "（）［］｛｝＜＞＂＇｀＠＃＄％＆＊＋－／＝＾＿｜～￥"       // 全角 ASCII 变体
    "‖§¶†‡※‐‑‒–――‥"
    "♪♭♯★☆○●◎◇◆□■△▲▽▼→←↑↓↔");
} // namespace

bool isPunct(QChar c)
{
    return kPunct.contains(c) || c.isSpace();
}

QString stripPunctuation(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (const QChar c : s) {
        if (!kPunct.contains(c))
            out.append(c);
    }
    return out.trimmed();
}

QStringList tokenizeWords(const QString &s)
{
    QStringList words;
    QString cur;
    for (const QChar c : s) {
        if (c.isLetterOrNumber() || c == '\'' || c == '-') {
            cur.append(c);
        } else {
            if (!cur.isEmpty()) {
                words << cur;
                cur.clear();
            }
        }
    }
    if (!cur.isEmpty())
        words << cur;
    return words;
}

QStringList splitToWordChips(const QString &s)
{
    // 把词连同紧随其后的标点作为一个 chip，便于造句重组后还原
    QStringList chips;
    QString cur;
    bool prevAlpha = false;
    for (const QChar c : s) {
        const bool alpha = c.isLetterOrNumber();
        if (!alpha && !prevAlpha && !c.isSpace()) {
            // 标点开头（如 '('）并入下一个词
            if (!cur.isEmpty()) {
                chips << cur;
                cur.clear();
            }
            cur.append(c);
            prevAlpha = false;
            continue;
        }
        if (!alpha && c.isSpace()) {
            if (!cur.isEmpty()) {
                chips << cur;
                cur.clear();
            }
            prevAlpha = false;
            continue;
        }
        cur.append(c);
        prevAlpha = alpha;
    }
    if (!cur.isEmpty())
        chips << cur;
    return chips;
}

int levenshtein(const QString &a, const QString &b)
{
    const int n = a.size(), m = b.size();
    QVector<int> prev(m + 1), cur(m + 1);
    for (int j = 0; j <= m; ++j)
        prev[j] = j;
    for (int i = 1; i <= n; ++i) {
        cur[0] = i;
        for (int j = 1; j <= m; ++j) {
            const int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            cur[j] = qMin(qMin(prev[j] + 1, cur[j - 1] + 1), prev[j - 1] + cost);
        }
        qSwap(prev, cur);
    }
    return prev[m];
}

QString normalizeForCheck(const QString &s, bool ignorePunctuation, bool ignoreCase)
{
    QString out;
    for (const QChar c : s) {
        if (ignorePunctuation && (isPunct(c) || c.isSpace()))
            continue;
        out.append(ignoreCase ? c.toCaseFolded() : c);
    }
    return out;
}

} // namespace adoloop::strutil
