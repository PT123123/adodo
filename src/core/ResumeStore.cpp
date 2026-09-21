#include "ResumeStore.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace adoloop {

namespace {

bool sameKey(const QString &a, const QString &b)
{
#ifdef Q_OS_WIN
    return a.compare(b, Qt::CaseInsensitive) == 0;
#else
    return a == b;
#endif
}

} // namespace

ResumeStore::ResumeStore(QObject *parent)
    : QObject(parent)
{
}

bool ResumeStore::contains(const QString &media) const
{
    if (media.isEmpty())
        return false;
    if (m_map.contains(media))
        return true;
    for (auto it = m_map.constBegin(); it != m_map.constEnd(); ++it) {
        if (sameKey(it.key(), media))
            return true;
    }
    return false;
}

ResumeStore::Entry ResumeStore::entry(const QString &media) const
{
    if (media.isEmpty())
        return {};
    const auto exact = m_map.constFind(media);
    if (exact != m_map.constEnd())
        return exact.value();
    for (auto it = m_map.constBegin(); it != m_map.constEnd(); ++it) {
        if (sameKey(it.key(), media))
            return it.value();
    }
    return {};
}

Ms ResumeStore::position(const QString &media) const
{
    return entry(media).positionMs;
}

void ResumeStore::record(const QString &media, Ms positionMs, Ms durationMs)
{
    if (media.isEmpty() || positionMs < 0) {
        return;
    }
    // 已播到结尾：清掉记录，下次从头开始（避免「续播到最后一秒」的脏数据）
    if (durationMs > 0 && durationMs - positionMs <= EofTailMs) {
        forget(media);
        return;
    }

    Entry e;
    e.positionMs = positionMs;
    e.durationMs = qMax<Ms>(0, durationMs);
    e.updatedAt = QDateTime::currentMSecsSinceEpoch();

    // 已存在（含大小写不敏感的等价键）：原地更新，避免同一文件两份记录
    for (auto it = m_map.begin(); it != m_map.end(); ++it) {
        if (sameKey(it.key(), media)) {
            it.value() = e;
            emit changed();
            return;
        }
    }
    m_map.insert(media, e);
    prune();
    emit changed();
}

void ResumeStore::forget(const QString &media)
{
    if (media.isEmpty())
        return;
    bool removed = false;
    for (auto it = m_map.begin(); it != m_map.end();) {
        if (sameKey(it.key(), media)) {
            it = m_map.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }
    if (removed)
        emit changed();
}

void ResumeStore::clear()
{
    if (m_map.isEmpty())
        return;
    m_map.clear();
    emit changed();
}

void ResumeStore::prune()
{
    while (m_map.size() > MaxEntries) {
        auto oldest = m_map.begin();
        for (auto it = m_map.begin(); it != m_map.end(); ++it) {
            if (it.value().updatedAt < oldest.value().updatedAt)
                oldest = it;
        }
        m_map.erase(oldest);
    }
}

bool ResumeStore::loadFrom(const QString &path)
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
        const QString media = o.value(QStringLiteral("media")).toString().trimmed();
        const Ms pos = Ms(o.value(QStringLiteral("positionMs")).toDouble());
        if (media.isEmpty() || pos < 0)
            continue; // 脏数据防护
        Entry e;
        e.positionMs = pos;
        e.durationMs = Ms(o.value(QStringLiteral("durationMs")).toDouble());
        e.updatedAt = qint64(o.value(QStringLiteral("updatedAt")).toDouble());
        m_map.insert(media, e);
    }
    prune();
    emit changed();
    return true;
}

bool ResumeStore::saveTo(const QString &path) const
{
    QJsonArray arr;
    for (auto it = m_map.constBegin(); it != m_map.constEnd(); ++it) {
        QJsonObject o;
        o.insert(QStringLiteral("media"), it.key());
        o.insert(QStringLiteral("positionMs"), double(it.value().positionMs));
        o.insert(QStringLiteral("durationMs"), double(it.value().durationMs));
        o.insert(QStringLiteral("updatedAt"), double(it.value().updatedAt));
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
