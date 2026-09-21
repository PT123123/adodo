#include "JapaneseInput.h"

#include <QHash>

namespace adoloop::japaneseinput {

namespace {

struct Entry {
    const char *roman;
    const char *kana;
};

// 罗马字 → 平假名（IME 常用规则；查表时按 3→2→1 字符最长匹配）
const Entry kTable[] = {
    // 元音
    {"a", "あ"}, {"i", "い"}, {"u", "う"}, {"e", "え"}, {"o", "お"},
    // 小写假名（x / l 前缀）
    {"xa", "ぁ"}, {"xi", "ぃ"}, {"xu", "ぅ"}, {"xe", "ぇ"}, {"xo", "ぉ"},
    {"la", "ぁ"}, {"li", "ぃ"}, {"lu", "ぅ"}, {"le", "ぇ"}, {"lo", "ぉ"},
    {"xya", "ゃ"}, {"xyu", "ゅ"}, {"xyo", "ょ"},
    {"lya", "ゃ"}, {"lyu", "ゅ"}, {"lyo", "ょ"},
    {"xtu", "っ"}, {"ltu", "っ"}, {"xtsu", "っ"}, {"ltsu", "っ"},
    {"xwa", "ゎ"}, {"lwa", "ゎ"},
    // や行
    {"ya", "や"}, {"yu", "ゆ"}, {"yo", "よ"},
    // わ行
    {"wa", "わ"}, {"wi", "うぃ"}, {"wu", "う"}, {"we", "うぇ"}, {"wo", "を"},
    // か行
    {"ka", "か"}, {"ki", "き"}, {"ku", "く"}, {"ke", "け"}, {"ko", "こ"},
    {"kya", "きゃ"}, {"kyi", "きぃ"}, {"kyu", "きゅ"}, {"kye", "きぇ"}, {"kyo", "きょ"},
    {"kwa", "くゎ"},
    // が行
    {"ga", "が"}, {"gi", "ぎ"}, {"gu", "ぐ"}, {"ge", "げ"}, {"go", "ご"},
    {"gya", "ぎゃ"}, {"gyi", "ぎぃ"}, {"gyu", "ぎゅ"}, {"gye", "ぎぇ"}, {"gyo", "ぎょ"},
    // さ行（sha/shu/sho 与 sya/syu/syo 等价）
    {"sa", "さ"}, {"si", "し"}, {"su", "す"}, {"se", "せ"}, {"so", "そ"},
    {"sha", "しゃ"}, {"shi", "し"}, {"shu", "しゅ"}, {"she", "しぇ"}, {"sho", "しょ"},
    {"sya", "しゃ"}, {"syi", "しぃ"}, {"syu", "しゅ"}, {"sye", "しぇ"}, {"syo", "しょ"},
    {"swa", "すぁ"}, {"swi", "すぃ"}, {"swu", "すぅ"}, {"swe", "すぇ"}, {"swo", "すぉ"},
    // ざ行
    {"za", "ざ"}, {"zi", "じ"}, {"zu", "ず"}, {"ze", "ぜ"}, {"zo", "ぞ"},
    {"zya", "じゃ"}, {"zyi", "じぃ"}, {"zyu", "じゅ"}, {"zye", "じぇ"}, {"zyo", "じょ"},
    // じゃ行（ja/ju/jo）
    {"ja", "じゃ"}, {"ji", "じ"}, {"ju", "じゅ"}, {"je", "じぇ"}, {"jo", "じょ"},
    {"jya", "じゃ"}, {"jyi", "じぃ"}, {"jyu", "じゅ"}, {"jye", "じぇ"}, {"jyo", "じょ"},
    // た行
    {"ta", "た"}, {"ti", "ち"}, {"tu", "つ"}, {"te", "て"}, {"to", "と"},
    {"cha", "ちゃ"}, {"chi", "ち"}, {"chu", "ちゅ"}, {"che", "ちぇ"}, {"cho", "ちょ"},
    {"tya", "ちゃ"}, {"tyi", "ちぃ"}, {"tyu", "ちゅ"}, {"tye", "ちぇ"}, {"tyo", "ちょ"},
    {"tsu", "つ"}, {"tsa", "つぁ"}, {"tsi", "つぃ"}, {"tse", "つぇ"}, {"tso", "つぉ"},
    {"tha", "てゃ"}, {"thi", "てぃ"}, {"thu", "てゅ"}, {"the", "てぇ"}, {"tho", "てょ"},
    // だ行
    {"da", "だ"}, {"di", "ぢ"}, {"du", "づ"}, {"de", "で"}, {"do", "ど"},
    {"dya", "ぢゃ"}, {"dyi", "ぢぃ"}, {"dyu", "ぢゅ"}, {"dye", "ぢぇ"}, {"dyo", "ぢょ"},
    {"dha", "でゃ"}, {"dhi", "でぃ"}, {"dhu", "でゅ"}, {"dhe", "でぇ"}, {"dho", "でょ"},
    // な行
    {"na", "な"}, {"ni", "に"}, {"nu", "ぬ"}, {"ne", "ね"}, {"no", "の"},
    {"nya", "にゃ"}, {"nyi", "にぃ"}, {"nyu", "にゅ"}, {"nye", "にぇ"}, {"nyo", "にょ"},
    // は行
    {"ha", "は"}, {"hi", "ひ"}, {"hu", "ふ"}, {"he", "へ"}, {"ho", "ほ"},
    {"hya", "ひゃ"}, {"hyi", "ひぃ"}, {"hyu", "ひゅ"}, {"hye", "ひぇ"}, {"hyo", "ひょ"},
    // ふ行（f）
    {"fa", "ふぁ"}, {"fi", "ふぃ"}, {"fu", "ふ"}, {"fe", "ふぇ"}, {"fo", "ふぉ"},
    {"fya", "ふゃ"}, {"fyu", "ふゅ"}, {"fyo", "ふょ"},
    // ば行
    {"ba", "ば"}, {"bi", "び"}, {"bu", "ぶ"}, {"be", "べ"}, {"bo", "ぼ"},
    {"bya", "びゃ"}, {"byi", "びぃ"}, {"byu", "びゅ"}, {"bye", "びぇ"}, {"byo", "びょ"},
    // ぱ行
    {"pa", "ぱ"}, {"pi", "ぴ"}, {"pu", "ぷ"}, {"pe", "ぺ"}, {"po", "ぽ"},
    {"pya", "ぴゃ"}, {"pyi", "ぴぃ"}, {"pyu", "ぴゅ"}, {"pye", "ぴぇ"}, {"pyo", "ぴょ"},
    // ま行
    {"ma", "ま"}, {"mi", "み"}, {"mu", "む"}, {"me", "め"}, {"mo", "も"},
    {"mya", "みゃ"}, {"myi", "みぃ"}, {"myu", "みゅ"}, {"mye", "みぇ"}, {"myo", "みょ"},
    // ら行
    {"ra", "ら"}, {"ri", "り"}, {"ru", "る"}, {"re", "れ"}, {"ro", "ろ"},
    {"rya", "りゃ"}, {"ryi", "りぃ"}, {"ryu", "りゅ"}, {"rye", "りぇ"}, {"ryo", "りょ"},
    // ゔ
    {"vu", "ゔ"}, {"va", "ゔぁ"}, {"vi", "ゔぃ"}, {"ve", "ゔぇ"}, {"vo", "ゔぉ"},
    {"vya", "ゔゃ"}, {"vyu", "ゔゅ"}, {"vyo", "ゔょ"},
    // 拨音（单写 n 结尾/后接非元音时由逻辑处理，这里补齐 nn / n'）
    {"nn", "ん"}, {"n'", "ん"},
};

const QHash<QString, QString> &table()
{
    static const QHash<QString, QString> t = []() {
        QHash<QString, QString> out;
        for (const Entry &e : kTable)
            out.insert(QString::fromLatin1(e.roman), QString::fromUtf8(e.kana));
        return out;
    }();
    return t;
}

bool isVowel(QChar c)
{
    const QChar l = c.toLower();
    return l == QLatin1Char('a') || l == QLatin1Char('i') || l == QLatin1Char('u')
        || l == QLatin1Char('e') || l == QLatin1Char('o');
}

bool isConsonant(QChar c)
{
    const QChar l = c.toLower();
    if (l < QLatin1Char('a') || l > QLatin1Char('z'))
        return false;
    return !isVowel(l);
}

} // namespace

QString katakanaToHiragana(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (const QChar c : s) {
        const ushort u = c.unicode();
        if (u >= 0x30A1 && u <= 0x30F6)
            out.append(QChar(u - 0x60));
        else
            out.append(c);
    }
    return out;
}

QString hiraganaToKatakana(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (const QChar c : s) {
        const ushort u = c.unicode();
        if (u >= 0x3041 && u <= 0x3096)
            out.append(QChar(u + 0x60));
        else
            out.append(c);
    }
    return out;
}

QString romajiToHiragana(const QString &romaji)
{
    const QString src = romaji.toLower();
    QString out;
    out.reserve(src.size());

    for (int i = 0; i < src.size();) {
        const QChar c = src.at(i);
        const QChar next = (i + 1 < src.size()) ? src.at(i + 1) : QChar();

        // 長音符：- → ー
        if (c == QLatin1Char('-') || c == QChar(0x30FC)) {
            out.append(QChar(0x30FC));
            ++i;
            continue;
        }
        if (!isConsonant(c) && !isVowel(c)) {
            out.append(romaji.at(i)); // 非字母（空格/汉字/假名/标点）原样保留
            ++i;
            continue;
        }

        // 拨音 ん
        if (c == QLatin1Char('n')) {
            if (!next.isNull() && next == QLatin1Char('\'')) {
                out.append(QStringLiteral("ん"));
                i += 2;
                continue;
            }
            if (next.isNull()) {
                out.append(QStringLiteral("ん"));
                ++i;
                continue;
            }
            if (!isVowel(next) && next != QLatin1Char('y')) {
                // n 后接辅音（含 nn 后再接辅音）：ん，只吃掉一个 n
                out.append(QStringLiteral("ん"));
                ++i;
                continue;
            }
            // 后接元音/y：交给查表（na / nya …）
        }

        // 拨音（m 在 b/p/m 前，如 shimbun）
        if (c == QLatin1Char('m') && (next == QLatin1Char('b') || next == QLatin1Char('p')
                                      || next == QLatin1Char('m'))) {
            out.append(QStringLiteral("ん"));
            ++i;
            continue;
        }

        // 促音 っ：tch 与双写辅音（n 除外）
        if (c == QLatin1Char('t') && next == QLatin1Char('c')
            && i + 2 < src.size() && src.at(i + 2) == QLatin1Char('h')) {
            out.append(QStringLiteral("っ"));
            ++i;
            continue;
        }
        if (isConsonant(c) && c != QLatin1Char('n') && next == c) {
            out.append(QStringLiteral("っ"));
            ++i;
            continue;
        }

        // 最长匹配（3 → 2 → 1）
        bool matched = false;
        for (int len = 3; len >= 1; --len) {
            if (i + len > src.size())
                continue;
            const QString key = src.mid(i, len);
            const auto it = table().constFind(key);
            if (it != table().constEnd()) {
                out.append(it.value());
                i += len;
                matched = true;
                break;
            }
        }
        if (matched)
            continue;

        out.append(romaji.at(i)); // 无法识别的字母原样保留
        ++i;
    }
    return out;
}

QString romajiToKatakana(const QString &romaji)
{
    return hiraganaToKatakana(romajiToHiragana(romaji));
}

bool looksLikeRomaji(const QString &s)
{
    bool hasLetter = false;
    for (const QChar c : s) {
        const ushort u = c.unicode();
        if ((u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z')) {
            hasLetter = true;
            continue;
        }
        if (u == ' ' || u == '\t' || u == '-' || u == '\'')
            continue;
        if (u >= '0' && u <= '9')
            continue;
        return false; // 出现非 ASCII / 假名 / 汉字 → 不是罗马字输入
    }
    return hasLetter;
}

bool containsKana(const QString &s)
{
    for (const QChar c : s) {
        const ushort u = c.unicode();
        if (u >= 0x3041 && u <= 0x309F)
            return true;
        if (u >= 0x30A1 && u <= 0x30FF)
            return true;
    }
    return false;
}

QString normalizeQuery(const QString &input)
{
    const QString s = input.trimmed();
    if (s.isEmpty())
        return {};
    if (looksLikeRomaji(s)) {
        const QString kana = romajiToHiragana(s);
        if (!kana.isEmpty() && kana != s)
            return kana;
    }
    return s;
}

} // namespace adoloop::japaneseinput
