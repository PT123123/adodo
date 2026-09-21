#include "VocabularyModel.h"
#include "../core/ReadingStore.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace adoloop {

VocabularyModel::VocabularyModel(QObject *parent)
    : QObject(parent)
{
}

int VocabularyModel::indexOf(const QString &word) const
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].word.compare(word, Qt::CaseInsensitive) == 0)
            return i;
    }
    return -1;
}

void VocabularyModel::setReadingStore(const ReadingStore *store)
{
    m_readings = store;
    fillMissingReadings(); // 已装载的旧条目也补上读音
}

int VocabularyModel::fillMissingReadings()
{
    if (!m_readings)
        return 0;
    int n = 0;
    for (VocabularyEntry &e : m_entries) {
        if (e.word.isEmpty() || !e.reading.isEmpty())
            continue;
        const ReadingInfo ri = m_readings->info(e.word);
        if (ri.reading.isEmpty())
            continue;
        e.reading = ri.reading;
        if (e.accent.isEmpty())
            e.accent = ri.accent;
        if (e.pos.isEmpty())
            e.pos = ri.pos;
        if (e.lang.isEmpty())
            e.lang = ri.lang.isEmpty() ? QStringLiteral("ja") : ri.lang;
        ++n;
    }
    if (n > 0)
        emit changed();
    return n;
}

void VocabularyModel::addWord(const QString &word, const QString &sentence, const QString &source,
                              const ReadingInfo &info)
{
    const QString w = word.trimmed();
    if (w.isEmpty())
        return;
    // M12：读音缺项时回落到读音缓存（查词成功时已写入），保证日语生词带上假名/声调/词性
    ReadingInfo ri = info;
    if (ri.reading.isEmpty() && m_readings) {
        const ReadingInfo cached = m_readings->info(w);
        if (ri.accent.isEmpty())
            ri.accent = cached.accent;
        if (ri.pos.isEmpty())
            ri.pos = cached.pos;
        if (ri.lang.isEmpty())
            ri.lang = cached.lang;
        ri.reading = cached.reading;
    }
    if (ri.lang.isEmpty())
        ri.lang = QStringLiteral("ja");

    const int idx = indexOf(w);
    if (idx >= 0) {
        VocabularyEntry &e = m_entries[idx];
        // 已有条目：只补空缺，不覆盖（后续查词可能给出更完整的读音）
        if (e.reading.isEmpty())
            e.reading = ri.reading;
        if (e.accent.isEmpty())
            e.accent = ri.accent;
        if (e.pos.isEmpty())
            e.pos = ri.pos;
        if (e.lang.isEmpty())
            e.lang = ri.lang;
        // 避免重复上下文
        for (const auto &c : e.contexts) {
            if (c.first == sentence)
                return;
        }
        e.contexts.append({sentence, source});
        if (e.contexts.size() > 20)
            e.contexts.removeFirst();
        e.level = qMin(5, e.level + 1);
    } else {
        VocabularyEntry e;
        e.word = w;
        e.addedAt = QDateTime::currentDateTime();
        e.level = 1;
        e.reading = ri.reading;
        e.accent = ri.accent;
        e.pos = ri.pos;
        e.lang = ri.lang;
        e.contexts.append({sentence, source});
        e.due = QDateTime::currentMSecsSinceEpoch();
        e.interval = 0.0;
        e.ease = 2.5;
        m_entries.append(e);
    }
    emit changed();
}

bool VocabularyModel::contains(const QString &word) const
{
    return indexOf(word) >= 0;
}

void VocabularyModel::markLevel(const QString &word, int level)
{
    const int idx = indexOf(word);
    if (idx < 0)
        return;
    m_entries[idx].level = qBound(1, level, 5);
    emit changed();
}

void VocabularyModel::removeWord(const QString &word)
{
    const int idx = indexOf(word);
    if (idx < 0)
        return;
    m_entries.removeAt(idx);
    emit changed();
}

const VocabularyEntry *VocabularyModel::entry(const QString &word) const
{
    const int idx = indexOf(word);
    return idx >= 0 ? &m_entries[idx] : nullptr;
}

QVector<const VocabularyEntry *> VocabularyModel::dueEntries() const
{
    QVector<const VocabularyEntry *> out;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const VocabularyEntry &e : m_entries) {
        if (e.due <= now)
            out << &e;
    }
    return out;
}

void VocabularyModel::applySm2(VocabularyEntry &e, int quality)
{
    quality = qBound(0, quality, 5);
    if (quality < 3) {
        e.interval = 0.0;
        e.lapses++;
        e.ease = qMax(1.3, e.ease - 0.2);
    } else {
        if (e.interval == 0.0)
            e.interval = 1.0;
        else if (e.interval == 1.0)
            e.interval = 6.0;
        else
            e.interval *= e.ease;
        e.ease += (0.1 - (5 - quality) * (0.08 + (5 - quality) * 0.02));
        e.ease = qMax(1.3, e.ease);
    }
    const int days = int(e.interval);
    e.due = QDateTime::currentMSecsSinceEpoch() + qint64(days) * 86400000;
}

void VocabularyModel::review(const QString &word, int quality)
{
    const int idx = indexOf(word);
    if (idx < 0)
        return;
    applySm2(m_entries[idx], quality);
    emit changed();
}

bool VocabularyModel::loadFrom(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    f.close();
    if (perr.error != QJsonParseError::NoError || !doc.isArray())
        return false;

    m_entries.clear();
    for (const QJsonValue &v : doc.array()) {
        const QJsonObject o = v.toObject();
        VocabularyEntry e;
        e.word = o.value(QStringLiteral("word")).toString();
        e.addedAt = QDateTime::fromMSecsSinceEpoch(qint64(o.value(QStringLiteral("addedAt")).toDouble()));
        e.level = o.value(QStringLiteral("level")).toInt(1);
        // M12：旧数据无这些字段 → 留空（不报错、不丢弃条目）
        e.reading = o.value(QStringLiteral("reading")).toString();
        e.accent = o.value(QStringLiteral("accent")).toString();
        e.pos = o.value(QStringLiteral("pos")).toString();
        e.lang = o.value(QStringLiteral("lang")).toString();
        e.due = qint64(o.value(QStringLiteral("due")).toDouble());
        e.interval = o.value(QStringLiteral("interval")).toDouble();
        e.ease = o.value(QStringLiteral("ease")).toDouble(2.5);
        e.lapses = o.value(QStringLiteral("lapses")).toInt();
        const QJsonArray ctx = o.value(QStringLiteral("contexts")).toArray();
        for (const QJsonValue &cv : ctx) {
            const QJsonObject co = cv.toObject();
            e.contexts.append({co.value(QStringLiteral("sentence")).toString(),
                               co.value(QStringLiteral("source")).toString()});
        }
        if (!e.word.isEmpty())
            m_entries.append(e);
    }
    fillMissingReadings(); // M12：旧条目缺读音时用读音缓存补齐（缓存为空则不动）
    emit changed();
    return true;
}

bool VocabularyModel::saveTo(const QString &path) const
{
    QJsonArray arr;
    for (const VocabularyEntry &e : m_entries) {
        QJsonObject o;
        o.insert(QStringLiteral("word"), e.word);
        o.insert(QStringLiteral("addedAt"), double(e.addedAt.toMSecsSinceEpoch()));
        o.insert(QStringLiteral("level"), e.level);
        o.insert(QStringLiteral("reading"), e.reading);
        o.insert(QStringLiteral("accent"), e.accent);
        o.insert(QStringLiteral("pos"), e.pos);
        o.insert(QStringLiteral("lang"), e.lang);
        o.insert(QStringLiteral("due"), double(e.due));
        o.insert(QStringLiteral("interval"), e.interval);
        o.insert(QStringLiteral("ease"), e.ease);
        o.insert(QStringLiteral("lapses"), e.lapses);
        QJsonArray ctx;
        for (const auto &c : e.contexts) {
            QJsonObject co;
            co.insert(QStringLiteral("sentence"), c.first);
            co.insert(QStringLiteral("source"), c.second);
            ctx.append(co);
        }
        o.insert(QStringLiteral("contexts"), ctx);
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
