#include "Tokenizer.h"
#include "../util/StringUtil.h"
#include "../util/Subprocess.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace adoloop {

namespace {

// ---------- 字符类别 ----------

enum class RunKind { Kanji, Hiragana, Katakana, Latin, Digit, Other };

RunKind classifyBmp(QChar c)
{
    if (Tokenizer::isKanjiChar(c))
        return RunKind::Kanji;
    if (Tokenizer::isHiraganaChar(c))
        return RunKind::Hiragana;
    if (Tokenizer::isKatakanaChar(c))
        return RunKind::Katakana;
    if (Tokenizer::isLatinChar(c))
        return RunKind::Latin;
    if (c.isDigit())
        return RunKind::Digit;
    return RunKind::Other;
}

RunKind classifyCodePoint(uint cp)
{
    // CJK 扩展 B 及以上（代理对）：按汉字处理（日文人名/难读字）
    if (cp > 0xFFFF)
        return (cp >= 0x20000 && cp <= 0x3FFFF) ? RunKind::Kanji : RunKind::Other;
    return classifyBmp(QChar(ushort(cp)));
}

struct Run {
    int begin = 0;
    int len = 0;
    RunKind kind = RunKind::Other;
};

// 按 Unicode 字符类别切 run；「ー」紧跟前一平假名 run 时并入（らーめん / コーヒー 两种都合理）
QVector<Run> splitRuns(const QString &t)
{
    QVector<Run> runs;
    RunKind curKind = RunKind::Other;
    int curBegin = 0;
    int curLen = 0;
    auto flush = [&]() {
        if (curLen > 0)
            runs << Run{curBegin, curLen, curKind};
        curLen = 0;
    };

    for (int i = 0; i < t.size();) {
        const QChar c = t.at(i);
        const uint cp = c.isHighSurrogate() && i + 1 < t.size() && t.at(i + 1).isLowSurrogate()
            ? QChar::surrogateToUcs4(c, t.at(i + 1))
            : uint(c.unicode());
        const int step = cp > 0xFFFF ? 2 : 1;
        RunKind k = classifyCodePoint(cp);
        if (cp == 0x30FC && curLen > 0 && curKind == RunKind::Hiragana)
            k = RunKind::Hiragana; // 長音符并入前邻平假名 run
        if (curLen > 0 && k != curKind)
            flush();
        if (curLen == 0) {
            curBegin = i;
            curKind = k;
        }
        curLen += step;
        i += step;
    }
    flush();
    return runs;
}

// 助词/助动词白名单：整串命中则不并入前邻汉字 run
const QStringList &particleList()
{
    static const QStringList kList = {
        QStringLiteral("は"), QStringLiteral("が"), QStringLiteral("を"), QStringLiteral("に"),
        QStringLiteral("で"), QStringLiteral("と"), QStringLiteral("も"), QStringLiteral("へ"),
        QStringLiteral("や"), QStringLiteral("の"), QStringLiteral("か"), QStringLiteral("ね"),
        QStringLiteral("よ"), QStringLiteral("な"), QStringLiteral("です"), QStringLiteral("ます"),
        QStringLiteral("ました"), QStringLiteral("ません"), QStringLiteral("だ"),
        QStringLiteral("た"), QStringLiteral("て"), QStringLiteral("から"), QStringLiteral("まで"),
        QStringLiteral("より"), QStringLiteral("など"), QStringLiteral("ので"), QStringLiteral("のに"),
        QStringLiteral("けど"), QStringLiteral("し"), QStringLiteral("ば"), QStringLiteral("ながら"),
        QStringLiteral("たり"), QStringLiteral("って"),
    };
    return kList;
}

QSet<QString> &particleSet()
{
    static QSet<QString> s = []() {
        QSet<QString> out;
        for (const QString &w : particleList())
            out.insert(w);
        return out;
    }();
    return s;
}

Token makeToken(const QString &text, int begin, int len)
{
    Token t;
    t.surface = text.mid(begin, len);
    t.begin = begin;
    t.length = t.surface.size();
    return t;
}

// MeCab 读音/原形为片假名 → 统一转平假名（与词典 return-phrase 风格一致）
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

} // namespace

// ---------- 语言工具 ----------

Lang Tokenizer::langFromCode(const QString &code)
{
    const QString c = code.trimmed().toLower();
    if (c == QLatin1String("ja") || c == QLatin1String("jp") || c == QLatin1String("jpn")
        || c == QLatin1String("japanese") || c.startsWith(QLatin1String("ja-")))
        return Lang::Japanese;
    return Lang::English;
}

QString Tokenizer::langCode(Lang lang)
{
    return lang == Lang::Japanese ? QStringLiteral("ja") : QStringLiteral("en");
}

QString Tokenizer::langDisplayName(Lang lang)
{
    return lang == Lang::Japanese ? QStringLiteral("日本語") : QStringLiteral("English");
}

// ---------- 字符判定 ----------

bool Tokenizer::isKanjiChar(QChar c)
{
    const ushort u = c.unicode();
    // 々 〆 〇 〻（重复/叠字符号）与 ヵ ヶ（数え方助数词）按汉字处理
    if (u == 0x3005 || u == 0x3006 || u == 0x3007 || u == 0x303B)
        return true;
    if (u == 0x30F5 || u == 0x30F6)
        return true;
    if (u >= 0x4E00 && u <= 0x9FFF)   // CJK 统一表意文字
        return true;
    if (u >= 0x3400 && u <= 0x4DBF)   // 扩展 A
        return true;
    if (u >= 0xF900 && u <= 0xFAFF)   // 兼容表意文字
        return true;
    return false;
}

bool Tokenizer::isHiraganaChar(QChar c)
{
    const ushort u = c.unicode();
    if (u >= 0x3041 && u <= 0x3096)
        return true;
    if (u >= 0x3099 && u <= 0x309C) // 浊点/半浊点等组合符号
        return true;
    if (u == 0x309D || u == 0x309E)
        return true;
    return false;
}

bool Tokenizer::isKatakanaChar(QChar c)
{
    const ushort u = c.unicode();
    if (u >= 0x30A1 && u <= 0x30FA)
        return true;
    if (u == 0x30FC || u == 0x30FD || u == 0x30FE) // 長音符・繰り返し記号
        return true;
    if (u >= 0x31F0 && u <= 0x31FF) // 片假名扩展（小写アイウ…）
        return true;
    if (u >= 0xFF66 && u <= 0xFF9F) // 半角片假名
        return true;
    return false;
}

bool Tokenizer::isLatinChar(QChar c)
{
    const ushort u = c.unicode();
    if ((u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z'))
        return true;
    if (u >= 0x00C0 && u <= 0x024F) // 拉丁扩展 A/B（带音标字母）
        return true;
    if (u >= 0xFF21 && u <= 0xFF3A) // 全角 Ａ-Ｚ
        return true;
    if (u >= 0xFF41 && u <= 0xFF5A) // 全角 ａ-ｚ
        return true;
    return false;
}

bool Tokenizer::isParticleOrAuxiliary(const QString &hiraganaRun)
{
    return particleSet().contains(hiraganaRun);
}

namespace {

// 平假名 run 开头命中的最长白名单前缀（1..3 字）；空 = 不以助词/助动词开头。
// 例：「はいい」→「は」；「です」→「です」（整串）；「きます」→ 空。
// 这一步用于避免「今日はいい」被整块当成一个词。
QString longestParticlePrefix(const QString &hira)
{
    const int maxLen = qMin(3, hira.size());
    for (int n = maxLen; n >= 1; --n) {
        const QString p = hira.left(n);
        if (Tokenizer::isParticleOrAuxiliary(p))
            return p;
    }
    return {};
}

} // namespace

// ---------- 英语 ----------

QVector<Token> Tokenizer::englishTokens(const QString &text)
{
    // 与 strutil::tokenizeWords 完全一致的切分规则（字母数字 + 词内 ' 与 -），另记位置
    QVector<Token> out;
    int begin = -1;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        const bool wordChar = c.isLetterOrNumber() || c == QLatin1Char('\'') || c == QLatin1Char('-');
        if (wordChar) {
            if (begin < 0)
                begin = i;
        } else if (begin >= 0) {
            out << makeToken(text, begin, i - begin);
            begin = -1;
        }
    }
    if (begin >= 0)
        out << makeToken(text, begin, text.size() - begin);
    return out;
}

// ---------- 内置日语启发式（纯函数） ----------

QVector<Token> Tokenizer::heuristicJapaneseTokens(const QString &text)
{
    QVector<Token> out;
    QVector<Run> runs = splitRuns(text);
    for (int i = 0; i < runs.size(); ++i) {
        const Run r = runs[i];
        if (r.kind == RunKind::Other)
            continue; // 标点/空白/记号丢弃

        int len = r.len;
        if (r.kind == RunKind::Kanji && i + 1 < runs.size()
            && runs[i + 1].kind == RunKind::Hiragana) {
            const Run h = runs[i + 1];
            const QString hira = text.mid(h.begin, h.len);
            const QString pfx = longestParticlePrefix(hira);

            if (!pfx.isEmpty()) {
                // 平假名 run 以助词/助动词开头 → 汉字不合并（私は → 私/は）。
                // 前缀之后若还有内容，拆成独立 run 依次成词（今日はいい → 今日/は/いい）。
                // 注意：不消费该 run —— 下一轮循环会把它（或拆分后的前缀）作为独立词输出。
                const int plen = int(pfx.size());
                if (plen < hira.size()) {
                    runs[i + 1] = Run{h.begin, plen, RunKind::Hiragana};
                    runs.insert(i + 2, Run{h.begin + plen, h.len - plen, RunKind::Hiragana});
                }
            } else if (h.len <= 3) {
                len += h.len; // 送假名整体并入（食べる / 大きい / 行きます）
                ++i;
            } else {
                // 长平假名串：只并入 1 个送假名（保留词干），余下多为助动词
                // （食べなかった → 食べ/なかった；学生ではありません → 学生/ではありません）
                const int take = isParticleOrAuxiliary(hira.left(1)) ? 0 : 1;
                len += take;
                if (take > 0)
                    runs[i + 1] = Run{h.begin + take, h.len - take, RunKind::Hiragana};
            }
        }
        out << makeToken(text, r.begin, len);
    }
    return out;
}

// ---------- 公开入口 ----------

QStringList Tokenizer::tokenize(const QString &text, Lang lang)
{
    QStringList words;
    for (const Token &t : tokens(text, lang))
        words << t.surface;
    return words;
}

QVector<Token> Tokenizer::tokens(const QString &text, Lang lang)
{
    if (text.isEmpty())
        return {};
    return lang == Lang::Japanese ? heuristicJapaneseTokens(text) : englishTokens(text);
}

QStringList Tokenizer::splitChips(const QString &text, Lang lang)
{
    if (lang != Lang::Japanese)
        return strutil::splitToWordChips(text);

    const QVector<Token> ts = heuristicJapaneseTokens(text);
    if (ts.isEmpty())
        return {};
    QStringList chips;
    int prevEnd = 0;
    for (int i = 0; i < ts.size(); ++i) {
        const int limit = (i + 1 < ts.size()) ? ts[i + 1].begin : text.size();
        int end = ts[i].begin + ts[i].length;
        // 词后标点并入本 chip（造句重组后可还原标点），遇空白停止
        while (end < limit && !text.at(end).isSpace())
            ++end;
        const QString chip = text.mid(prevEnd, end - prevEnd).trimmed();
        if (!chip.isEmpty())
            chips << chip;
        prevEnd = end;
    }
    return chips;
}

// ---------- 外部引擎（MeCab / Python 桥） ----------

QString Tokenizer::defaultBridgeScriptPath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList cands = {
        QDir(appDir).filePath(QStringLiteral("scripts/japanese_tokenizer_bridge.py")),
        QDir(appDir).filePath(QStringLiteral("../scripts/japanese_tokenizer_bridge.py")),
        QDir(appDir).filePath(QStringLiteral("../../scripts/japanese_tokenizer_bridge.py")),
    };
    for (const QString &c : cands) {
        if (QFileInfo::exists(c))
            return QFileInfo(c).absoluteFilePath();
    }
    return cands.first();
}

QVector<QVector<Token>> Tokenizer::tryMecab(const QStringList &lines, const Options &opt, bool *ok)
{
    if (ok)
        *ok = false;
    QString prog;
    if (!Subprocess::findExecutable(opt.mecabPath, &prog))
        return {};

    const QByteArray input = (lines.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8();
    const Subprocess::Result r = Subprocess::runWithInput(prog, {}, input, opt.timeoutMs);
    if (!r.ok)
        return {};

    const QString outText = QString::fromUtf8(r.stdoutData);
    if (outText.contains(QChar(0xFFFD)))
        return {}; // 输出编码非 UTF-8（如 Shift-JIS 版 MeCab）→ 降级

    QVector<QVector<Token>> out;
    QVector<Token> cur;
    int pendingLine = 0;
    int curLine = -1;
    int cursor = 0;

    const QStringList outLines = outText.split(QLatin1Char('\n'));
    for (const QString &raw : outLines) {
        const QString line = raw.endsWith(QLatin1Char('\r')) ? raw.left(raw.size() - 1) : raw;
        if (line.isEmpty())
            continue;
        if (line == QLatin1String("EOS")) {
            if (pendingLine >= lines.size())
                return {};
            out << cur;
            cur.clear();
            curLine = -1;
            cursor = 0;
            ++pendingLine;
            continue;
        }
        const int tab = line.indexOf(QLatin1Char('\t'));
        const QString surface = tab >= 0 ? line.left(tab) : line;
        const QStringList feats = tab >= 0 ? line.mid(tab + 1).split(QLatin1Char(',')) : QStringList();
        if (surface.trimmed().isEmpty())
            continue; // 空白词：跳过（位置由 indexOf 兜底）
        if (curLine < 0)
            curLine = pendingLine;

        Token t;
        t.surface = surface;
        t.length = surface.size();
        if (feats.size() >= 8) {
            t.lemma = feats.at(6);
            t.reading = katakanaToHiragana(feats.at(7));
        }
        const QString src = lines.value(curLine);
        const int p = src.indexOf(surface, cursor);
        if (p >= 0) {
            t.begin = p;
            cursor = p + surface.size();
        }
        cur << t;
    }
    if (curLine >= 0 && !cur.isEmpty() && pendingLine < lines.size()) {
        out << cur;
        ++pendingLine;
    }
    if (out.size() != lines.size())
        return {}; // 输出与输入行数不符 → 认为不可靠，降级
    if (ok)
        *ok = true;
    return out;
}

QVector<QVector<Token>> Tokenizer::tryPythonBridge(const QStringList &lines, const Options &opt,
                                                   bool *ok)
{
    if (ok)
        *ok = false;
    QString py;
    if (!Subprocess::findExecutable(opt.pythonPath, &py))
        return {};
    const QString script = opt.bridgeScript.isEmpty() ? defaultBridgeScriptPath() : opt.bridgeScript;
    if (!QFileInfo::exists(script))
        return {};

    QByteArray input;
    for (const QString &l : lines) {
        QJsonObject req;
        req.insert(QStringLiteral("text"), l);
        input += QJsonDocument(req).toJson(QJsonDocument::Compact);
        input += '\n';
    }

    const Subprocess::Result r = Subprocess::runWithInput(
        py, {QStringLiteral("-X"), QStringLiteral("utf8"), script}, input, opt.timeoutMs);
    if (!r.ok)
        return {};

    QVector<QVector<Token>> out;
    const QStringList outLines = QString::fromUtf8(r.stdoutData).split(QLatin1Char('\n'));
    for (const QString &raw : outLines) {
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;
        QJsonParseError perr;
        const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject())
            return {}; // 混入非 JSON 输出（如 Python 警告）→ 降级
        const QJsonObject obj = doc.object();
        if (obj.contains(QStringLiteral("error")))
            return {};

        QVector<Token> toks;
        const QString src = lines.value(out.size());
        for (const QJsonValue &tv : obj.value(QStringLiteral("tokens")).toArray()) {
            const QJsonObject to = tv.toObject();
            Token t;
            t.surface = to.value(QStringLiteral("surface")).toString();
            t.reading = to.value(QStringLiteral("reading")).toString();
            t.lemma = to.value(QStringLiteral("lemma")).toString();
            t.length = t.surface.size();
            int begin = to.value(QStringLiteral("begin")).toInt(-1);
            if (begin < 0 || src.mid(begin, t.surface.size()) != t.surface)
                begin = src.indexOf(t.surface);
            t.begin = begin;
            if (!t.surface.isEmpty())
                toks << t;
        }
        out << toks;
    }
    if (out.size() != lines.size())
        return {};
    if (ok)
        *ok = true;
    return out;
}

QVector<QVector<Token>> Tokenizer::tokensExternalBatch(const QStringList &lines, Lang lang,
                                                       const Options &opt, QString *engineUsed)
{
    auto fallback = [&]() {
        QVector<QVector<Token>> built;
        for (const QString &l : lines)
            built << tokens(l, lang);
        if (engineUsed)
            *engineUsed = QStringLiteral("builtin");
        return built;
    };

    if (lang != Lang::Japanese || lines.isEmpty())
        return fallback();

    QStringList batch = lines;
    if (opt.maxBatchLines > 0 && batch.size() > opt.maxBatchLines)
        batch = batch.mid(0, opt.maxBatchLines);

    // ① MeCab：仅当用户在设置里填了路径/命令名时尝试
    if (!opt.mecabPath.trimmed().isEmpty()) {
        bool ok = false;
        const QVector<QVector<Token>> r = tryMecab(batch, opt, &ok);
        if (ok) {
            if (engineUsed)
                *engineUsed = QStringLiteral("mecab");
            return r;
        }
    }
    // ② Python 桥（janome / fugashi）：仅当用户填了 python 路径时尝试
    if (!opt.pythonPath.trimmed().isEmpty()) {
        bool ok = false;
        const QVector<QVector<Token>> r = tryPythonBridge(batch, opt, &ok);
        if (ok) {
            if (engineUsed)
                *engineUsed = QStringLiteral("python-bridge");
            return r;
        }
    }
    // ③ 内置启发式（默认档）
    return fallback();
}

QVector<Token> Tokenizer::tokensExternal(const QString &text, Lang lang, const Options &opt,
                                         QString *engineUsed)
{
    const QVector<QVector<Token>> r =
        tokensExternalBatch({text}, lang, opt, engineUsed);
    return r.isEmpty() ? QVector<Token>() : r.first();
}

} // namespace adoloop
