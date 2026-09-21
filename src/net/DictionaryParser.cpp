#include "DictionaryParser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QUrl>

namespace adoloop::dictparse {

namespace {

const QString kVoiceBase = QStringLiteral("https://dict.youdao.com/dictvoice?audio=");

// 有道的 l.i 在不同节点下可能是字符串，也可能是字符串数组（英语释义是数组）
QString lText(const QJsonValue &v)
{
    if (v.isString())
        return v.toString();
    if (v.isArray()) {
        for (const QJsonValue &x : v.toArray()) {
            const QString s = lText(x);
            if (!s.isEmpty())
                return s;
        }
        return {};
    }
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        if (o.contains(QStringLiteral("i")))
            return lText(o.value(QStringLiteral("i")));
        if (o.contains(QStringLiteral("l")))
            return lText(o.value(QStringLiteral("l")));
    }
    return {};
}

bool readRoot(const QByteArray &body, QJsonObject *root, QString *error)
{
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = QStringLiteral("词典响应解析失败");
        return false;
    }
    *root = doc.object();
    return true;
}

// 声调：有道返回 "0"（平板）或 "②" 之类；统一成 ①②③…（0 表示平板，原样保留）
QString normalizeAccent(const QString &raw)
{
    const QString a = raw.trimmed();
    if (a.isEmpty())
        return {};
    static const QStringList kCircled = {QStringLiteral("⓪"), QStringLiteral("①"), QStringLiteral("②"),
                                         QStringLiteral("③"), QStringLiteral("④"), QStringLiteral("⑤"),
                                         QStringLiteral("⑥"), QStringLiteral("⑦"), QStringLiteral("⑧"),
                                         QStringLiteral("⑨"), QStringLiteral("⑩")};
    bool ok = false;
    const int n = a.toInt(&ok);
    if (ok && n >= 0 && n < kCircled.size())
        return kCircled.at(n);
    if (a.size() == 1 && a.at(0).unicode() >= 0x2460 && a.at(0).unicode() <= 0x2473)
        return a; // 已是 ①..⑳
    return a;
}

// 「[学生時代(じだい)] 学生时代」这类条目在 jc 节点里是用法/例句，单独归到 examples
bool looksLikeExample(const QString &text)
{
    const int close = text.indexOf(QStringLiteral("]"));
    if (close <= 0)
        return false;
    const QString inner = text.left(close + 1);
    return inner.startsWith(QLatin1Char('[')) && inner.size() >= 3 && text.size() > close + 1;
}

// 实测 jc 节点文本里会带 <sup>⑤</sup> 之类标签（弹窗不渲染 HTML）→ 去掉标签保留文字
QString stripTags(const QString &s)
{
    if (!s.contains(QLatin1Char('<')))
        return s;
    QString out;
    out.reserve(s.size());
    bool inTag = false;
    for (const QChar c : s) {
        if (c == QLatin1Char('<'))
            inTag = true;
        else if (c == QLatin1Char('>'))
            inTag = false;
        else if (!inTag)
            out.append(c);
    }
    return out.trimmed();
}

} // namespace

QString audioUrlFromSpeech(const QString &speech)
{
    // speech 是「已 URL 编码的假名 + &le=jap」（或英语的「word&type=1」），直接拼接即可
    const QString s = speech.trimmed();
    if (s.isEmpty())
        return {};
    return kVoiceBase + s;
}

namespace {

// speech 字段可用性（实测：多读音词条会用「・」分隔，如 た·べる，
// 该 URL 会被服务端拒绝 500；此时应改用表记/去掉分隔符的假名兜底）。
bool speechUsable(const QString &speech)
{
    const QString s = speech.trimmed();
    if (s.isEmpty())
        return false;
    if (s.contains(QChar(0x30FB)) || s.contains(QStringLiteral("%C2%B7"), Qt::CaseInsensitive)
        || s.contains(QStringLiteral("%B7"), Qt::CaseInsensitive))
        return false; // 中点分隔（U+30FB，UTF-8: E3 83 BB / 百分号编码）
    return true;
}

} // namespace

QString audioUrlForWord(const QString &word, Lang lang, int type)
{
    const QString w = word.trimmed();
    if (w.isEmpty())
        return {};
    const QString enc = QString::fromLatin1(QUrl::toPercentEncoding(w));
    if (lang == Lang::Japanese)
        return kVoiceBase + enc + QStringLiteral("&le=jap");
    return kVoiceBase + enc + QStringLiteral("&type=%1").arg(type);
}

bool parseJapaneseJson(const QByteArray &body, DictEntry *out, QString *error)
{
    QJsonObject root;
    if (!readRoot(body, &root, error))
        return false;

    const QString query = root.value(QStringLiteral("query")).toString();
    DictEntry e;
    e.lang = Lang::Japanese;

    const QJsonObject jc = root.value(QStringLiteral("jc")).toObject();
    const QJsonArray words = jc.value(QStringLiteral("word")).toArray();

    // 多候选词条：优先取 origin 与查询词完全相同的那条
    QJsonObject pick;
    for (const QJsonValue &wv : words) {
        const QJsonObject wo = wv.toObject();
        if (pick.isEmpty()) {
            pick = wo;
        } else if (!query.isEmpty()
                   && wo.value(QStringLiteral("origin")).toString() == query) {
            pick = wo;
            break;
        }
    }

    if (!pick.isEmpty()) {
        const QString origin = pick.value(QStringLiteral("origin")).toString();
        const QString reading = lText(pick.value(QStringLiteral("return-phrase"))
                                          .toObject().value(QStringLiteral("l")));
        e.word = !origin.isEmpty() ? origin : (!reading.isEmpty() ? reading : query);
        e.reading = reading;
        e.accent = normalizeAccent(pick.value(QStringLiteral("phonesup")).toString());

        // 发音：speech 已是 URL 编码的假名（+&le=jap），直接拼；
        // 含「・」多读音分隔符的 speech 会被服务端拒绝（实测 500）→ 用表记兜底
        const QString speech = pick.value(QStringLiteral("speech")).toString();
        if (speechUsable(speech))
            e.audioUrl = audioUrlFromSpeech(speech);
        if (e.audioUrl.isEmpty())
            e.audioUrl = audioUrlForWord(e.word.isEmpty() ? query : e.word, Lang::Japanese);

        const QJsonArray trs = pick.value(QStringLiteral("trs")).toArray();
        for (const QJsonValue &trv : trs) {
            const QJsonObject tro = trv.toObject();
            const QString pos = tro.value(QStringLiteral("pos")).toString();
            // M12：词性单独留一份（读音缓存/生词本/闪卡用；释义里仍带 [pos] 前缀）
            if (e.pos.isEmpty() && !pos.isEmpty())
                e.pos = pos;
            for (const QJsonValue &tv : tro.value(QStringLiteral("tr")).toArray()) {
                const QString text = stripTags(lText(tv.toObject().value(QStringLiteral("l"))));
                if (text.isEmpty())
                    continue;
                if (looksLikeExample(text)) {
                    e.examples << text;
                    continue;
                }
                e.definitions << (pos.isEmpty() ? text
                                                : QStringLiteral("[%1] %2").arg(pos, text));
            }
        }
        // 全是例句（无普通释义）时，把例句当释义用，保证弹窗有内容
        if (e.definitions.isEmpty() && !e.examples.isEmpty()) {
            e.definitions = e.examples;
            e.examples.clear();
        }
    } else {
        // 无 jc 节点（如片假名外来语）：退回 simple 节点，只给表记 + 中文对译
        const QJsonArray sw = root.value(QStringLiteral("simple"))
                                  .toObject().value(QStringLiteral("word")).toArray();
        for (const QJsonValue &sv : sw) {
            const QJsonObject so = sv.toObject();
            const QString phrase = so.value(QStringLiteral("return-phrase")).toString();
            const QString trans = stripTags(so.value(QStringLiteral("translation")).toString());
            if (e.word.isEmpty() && !phrase.isEmpty())
                e.word = phrase.split(QLatin1Char('#')).first().trimmed();
            if (!trans.isEmpty())
                e.definitions << trans;
        }
        if (e.word.isEmpty())
            e.word = query;
    }

    if (e.word.isEmpty())
        e.word = query;
    if (e.definitions.isEmpty()) {
        if (error)
            *error = QStringLiteral("未找到释义: %1").arg(e.word.isEmpty() ? query : e.word);
        return false;
    }
    if (e.audioUrl.isEmpty())
        e.audioUrl = audioUrlForWord(e.reading.isEmpty() ? e.word : e.reading, Lang::Japanese);

    *out = e;
    return true;
}

bool parseEnglishJson(const QByteArray &body, DictEntry *out, QString *error)
{
    QJsonObject root;
    if (!readRoot(body, &root, error))
        return false;

    // 英语路径（M11 修正在线响应结构，字段语义与 M10 一致）：
    //   实测 le=en 响应中 ec.word 是**数组**（旧版为对象），trs 在数组元素内部；
    //   return-phrase 既可能是字符串也可能是 {"l":{"i":...}}；顶层 query 可能缺失。
    //   两种形态都兼容，取不到任何释义时仍按 M10 报「未找到释义」。
    const QJsonObject ec = root.value(QStringLiteral("ec")).toObject();
    const QString query = root.value(QStringLiteral("query")).toString();

    QJsonObject wmeta;
    const QJsonArray wordArr = ec.value(QStringLiteral("word")).toArray();
    for (const QJsonValue &wv : wordArr) {
        const QJsonObject wo = wv.toObject();
        if (wmeta.isEmpty())
            wmeta = wo;
        const QString rp = lText(wo.value(QStringLiteral("return-phrase")));
        if (!query.isEmpty() && rp.compare(query, Qt::CaseInsensitive) == 0) {
            wmeta = wo; // 多词条时优先与查询词同形的那条
            break;
        }
    }
    if (wmeta.isEmpty())
        wmeta = ec.value(QStringLiteral("word")).toObject(); // 旧形态：对象

    DictEntry e;
    e.lang = Lang::English;
    e.word = query;
    if (e.word.isEmpty())
        e.word = lText(wmeta.value(QStringLiteral("return-phrase")));
    if (e.word.isEmpty()) {
        const QJsonArray sw = root.value(QStringLiteral("simple"))
                                  .toObject().value(QStringLiteral("word")).toArray();
        if (!sw.isEmpty())
            e.word = sw.first().toObject().value(QStringLiteral("return-phrase")).toString();
    }

    // 音标
    e.phoneticUs = wmeta.value(QStringLiteral("usphone")).toString();
    e.phoneticUk = wmeta.value(QStringLiteral("ukphone")).toString();
    if (e.phoneticUs.isEmpty())
        e.phoneticUs = wmeta.value(QStringLiteral("phone")).toString();

    // 释义（词性 + 义项）：新版在 ec.word[].trs（无 pos），旧版在 ec.trs
    auto collectTrs = [&e](const QJsonArray &trs) {
        for (const QJsonValue &trv : trs) {
            const QJsonObject tro = trv.toObject();
            // M12：旧形态的 pos 单独留一份（新版无该字段 → 留空）
            if (e.pos.isEmpty()) {
                const QString pos = tro.value(QStringLiteral("pos")).toString();
                if (!pos.isEmpty())
                    e.pos = pos;
            }
            const QJsonArray tr = tro.value(QStringLiteral("tr")).toArray();
            for (const QJsonValue &tv : tr) {
                const QString t = stripTags(lText(tv.toObject().value(QStringLiteral("l"))));
                if (!t.isEmpty() && !e.definitions.contains(t))
                    e.definitions << t;
            }
        }
    };
    collectTrs(wmeta.value(QStringLiteral("trs")).toArray());
    collectTrs(ec.value(QStringLiteral("trs")).toArray());

    // 例句（blng_sents_part）
    const QJsonArray sents = root.value(QStringLiteral("blng_sents_part"))
                                 .toObject().value(QStringLiteral("sentence-pair")).toArray();
    for (const QJsonValue &sv : sents) {
        const QJsonObject so = sv.toObject();
        const QString en = so.value(QStringLiteral("sentence")).toString();
        const QString zh = so.value(QStringLiteral("sentence-translation")).toString();
        if (!en.isEmpty())
            e.examples << (zh.isEmpty() ? en : en + QStringLiteral("\n") + zh);
        if (e.examples.size() >= 3)
            break;
    }

    if (!e.ok()) {
        if (error)
            *error = QStringLiteral("未找到释义: %1").arg(e.word.isEmpty() ? QStringLiteral("（空）") : e.word);
        return false;
    }

    // 发音（M11 朗读按钮）：优先词典给的 usspeech / ukspeech（实测 type=1 英音、type=2 美音）
    const QString usSpeech = wmeta.value(QStringLiteral("usspeech")).toString();
    const QString ukSpeech = wmeta.value(QStringLiteral("ukspeech")).toString();
    e.audioUs = speechUsable(usSpeech) ? audioUrlFromSpeech(usSpeech)
                                       : audioUrlForWord(e.word, Lang::English, 2);
    e.audioUk = speechUsable(ukSpeech) ? audioUrlFromSpeech(ukSpeech)
                                       : audioUrlForWord(e.word, Lang::English, 1);
    e.audioUrl = e.audioUs;
    *out = e;
    return true;
}

} // namespace adoloop::dictparse
