#include "ReadingStore.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace adoloop {

namespace {
// 上限：5000 条（约几十万字节 JSON）；超出淘汰最久未更新者
constexpr int kMaxEntries = 5000;
}

ReadingStore::ReadingStore(QObject *parent)
    : QObject(parent)
{
}

int ReadingStore::maxEntries()
{
    return kMaxEntries;
}

QString ReadingStore::keyOf(const QString &word)
{
    return word.trimmed().toLower();
}

bool ReadingStore::put(const Entry &entry, bool overwrite)
{
    const QString key = keyOf(entry.word);
    if (key.isEmpty() || entry.info.reading.isEmpty())
        return false; // 空词 / 无读音：脏数据，丢弃（查词失败不会走到这里）

    const auto it = m_map.constFind(key);
    if (it != m_map.constEnd() && !overwrite)
        return false; // 已有记录且不允许覆盖（词典优先于分词引擎）

    Entry e = entry;
    e.word = entry.word.trimmed();
    e.updatedAt = entry.updatedAt > 0 ? entry.updatedAt : QDateTime::currentMSecsSinceEpoch();
    // 覆盖时按字段合并：新值缺失的字段保留旧值，避免「后写的空值」把有用信息冲掉
    if (it != m_map.constEnd() && overwrite) {
        if (e.info.accent.isEmpty())
            e.info.accent = it->info.accent;
        if (e.info.pos.isEmpty())
            e.info.pos = it->info.pos;
        if (e.info.lang.isEmpty())
            e.info.lang = it->info.lang;
    }
    m_map.insert(key, e);
    evictIfNeeded();
    emit changed();
    return true;
}

bool ReadingStore::putReading(const QString &word, const QString &reading, const QString &accent,
                              const QString &pos, Lang lang, bool overwrite)
{
    Entry e;
    e.word = word.trimmed();
    e.info.reading = reading.trimmed();
    e.info.accent = accent.trimmed();
    e.info.pos = pos.trimmed();
    e.info.lang = Tokenizer::langCode(lang);
    return put(e, overwrite);
}

bool ReadingStore::putFromDict(const DictEntry &entry, Lang lang)
{
    // 查词失败（无释义）不得污染缓存；英语译文/音标路径不写读音缓存（保持 M11 行为）
    if (!entry.ok() || entry.reading.trimmed().isEmpty())
        return false;
    const QString word = entry.word.trimmed().isEmpty() ? entry.reading.trimmed()
                                                        : entry.word.trimmed();
    return putReading(word, entry.reading, entry.accent, entry.pos, lang, true);
}

int ReadingStore::putFromTokens(const QVector<Token> &tokens, Lang lang, bool overwrite)
{
    int n = 0;
    for (const Token &t : tokens) {
        if (t.surface.trimmed().isEmpty() || t.reading.trimmed().isEmpty())
            continue;
        if (putReading(t.surface, t.reading, QString(), QString(), lang, overwrite))
            ++n;
    }
    return n;
}

bool ReadingStore::contains(const QString &word) const
{
    return m_map.contains(keyOf(word));
}

const ReadingStore::Entry *ReadingStore::find(const QString &word) const
{
    const auto it = m_map.constFind(keyOf(word));
    return it == m_map.constEnd() ? nullptr : &it.value();
}

QString ReadingStore::reading(const QString &word) const
{
    const Entry *e = find(word);
    return e ? e->info.reading : QString();
}

QString ReadingStore::accent(const QString &word) const
{
    const Entry *e = find(word);
    return e ? e->info.accent : QString();
}

QString ReadingStore::pos(const QString &word) const
{
    const Entry *e = find(word);
    return e ? e->info.pos : QString();
}

ReadingInfo ReadingStore::info(const QString &word) const
{
    const Entry *e = find(word);
    return e ? e->info : ReadingInfo{};
}

void ReadingStore::clear()
{
    if (m_map.isEmpty())
        return;
    m_map.clear();
    emit changed();
}

void ReadingStore::evictIfNeeded()
{
    if (m_map.size() <= kMaxEntries)
        return;
    // 淘汰最久未更新的条目（条目数超限才会走到这里，O(n) 可接受）
    while (m_map.size() > kMaxEntries) {
        auto oldest = m_map.begin();
        for (auto it = m_map.begin(); it != m_map.end(); ++it) {
            if (it.value().updatedAt < oldest.value().updatedAt)
                oldest = it;
        }
        m_map.erase(oldest);
    }
}

QString ReadingStore::displayWithReading(const QString &word, const QString &reading)
{
    const QString r = reading.trimmed();
    if (r.isEmpty() || r == word)
        return word;
    return QStringLiteral("%1（%2）").arg(word, r);
}

QString ReadingStore::displayWithInfo(const QString &word, const ReadingInfo &info)
{
    const QString r = info.reading.trimmed();
    if (r.isEmpty())
        return word; // 无读音 → 调用方退回原样式
    QString out = QStringLiteral("%1 [%2]").arg(word, r);
    if (!info.accent.trimmed().isEmpty())
        out += QStringLiteral(" %1").arg(info.accent.trimmed());
    if (!info.pos.trimmed().isEmpty())
        out += QStringLiteral(" %1").arg(info.pos.trimmed());
    return out;
}

bool ReadingStore::loadFrom(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    f.close();
    if (perr.error != QJsonParseError::NoError || !doc.isArray())
        return false;

    m_map.clear();
    for (const QJsonValue &v : doc.array()) {
        const QJsonObject o = v.toObject();
        Entry e;
        e.word = o.value(QStringLiteral("word")).toString().trimmed();
        e.info.reading = o.value(QStringLiteral("reading")).toString().trimmed();
        e.info.accent = o.value(QStringLiteral("accent")).toString().trimmed();
        e.info.pos = o.value(QStringLiteral("pos")).toString().trimmed();
        e.info.lang = o.value(QStringLiteral("lang")).toString().trimmed();
        e.updatedAt = qint64(o.value(QStringLiteral("updatedAt")).toDouble());
        // 脏数据（空词/无读音）直接丢弃；重复键后者覆盖前者
        if (e.word.isEmpty() || e.info.reading.isEmpty())
            continue;
        if (e.info.lang.isEmpty())
            e.info.lang = QStringLiteral("ja");
        m_map.insert(keyOf(e.word), e);
    }
    emit changed();
    return true;
}

bool ReadingStore::saveTo(const QString &path) const
{
    QJsonArray arr;
    for (const Entry &e : m_map) {
        QJsonObject o;
        o.insert(QStringLiteral("word"), e.word);
        o.insert(QStringLiteral("reading"), e.info.reading);
        o.insert(QStringLiteral("accent"), e.info.accent);
        o.insert(QStringLiteral("pos"), e.info.pos);
        o.insert(QStringLiteral("lang"), e.info.lang);
        o.insert(QStringLiteral("updatedAt"), double(e.updatedAt));
        arr.append(o);
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    f.close();
    return true;
}

} // namespace adoloop
