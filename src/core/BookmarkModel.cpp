#include "BookmarkModel.h"
#include "../util/TimeUtil.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

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

BookmarkModel::BookmarkModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int BookmarkModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant BookmarkModel::data(const QModelIndex &index, int role) const
{
    const Bookmark *b = at(index.row());
    if (!index.isValid() || !b)
        return {};
    switch (role) {
    case TimeRole:
        return b->timeMs;
    case TimeTextRole:
        return timeutil::toClock(b->timeMs);
    case NoteRole:
        return b->note;
    case MediaRole:
        return b->media;
    case MediaTitleRole:
        return b->mediaTitle;
    default:
        return {};
    }
}

QHash<int, QByteArray> BookmarkModel::roleNames() const
{
    return {
        {TimeRole, "time"},
        {TimeTextRole, "timeText"},
        {NoteRole, "note"},
        {MediaRole, "media"},
        {MediaTitleRole, "mediaTitle"},
    };
}

bool BookmarkModel::sameMedia(const QString &a, const QString &b)
{
    if (a.isEmpty() || b.isEmpty())
        return false;
    return sameKey(a, b);
}

void BookmarkModel::setMediaFilter(const QString &media)
{
    if (sameKey(m_filter, media))
        return;
    beginResetModel();
    m_filter = media;
    rebuildRows();
    endResetModel();
    emit changed();
}

void BookmarkModel::rebuildRows()
{
    m_rows.clear();
    for (int i = 0; i < m_all.size(); ++i) {
        if (sameMedia(m_all[i].media, m_filter))
            m_rows << i;
    }
    std::sort(m_rows.begin(), m_rows.end(), [this](int a, int b) {
        if (m_all[a].timeMs != m_all[b].timeMs)
            return m_all[a].timeMs < m_all[b].timeMs;
        return m_all[a].createdAt < m_all[b].createdAt;
    });
}

int BookmarkModel::addBookmark(const QString &media, const QString &mediaTitle, Ms timeMs,
                               const QString &note)
{
    if (media.isEmpty() || timeMs < 0)
        return -1;

    Bookmark b;
    b.media = media;
    b.mediaTitle = mediaTitle;
    b.timeMs = timeMs;
    b.note = note.trimmed();
    b.createdAt = QDateTime::currentMSecsSinceEpoch();

    // 合并窗口：同一处的重复点击只补备注，不产生重复书签
    for (int i = 0; i < m_all.size(); ++i) {
        if (!sameMedia(m_all[i].media, media))
            continue;
        if (qAbs(m_all[i].timeMs - timeMs) <= MergeWindowMs) {
            if (!b.note.isEmpty())
                m_all[i].note = b.note;
            rebuildRows();
            emit changed();
            for (int row = 0; row < m_rows.size(); ++row) {
                if (m_rows[row] == i)
                    return row;
            }
            return -1;
        }
    }

    // 单文件上限：淘汰该文件最早的书签
    QVector<int> mine;
    for (int i = 0; i < m_all.size(); ++i) {
        if (sameMedia(m_all[i].media, media))
            mine << i;
    }
    if (mine.size() >= MaxPerMedia) {
        int oldest = mine.first();
        for (int i : mine) {
            if (m_all[i].createdAt < m_all[oldest].createdAt)
                oldest = i;
        }
        beginResetModel();
        m_all.removeAt(oldest);
        m_all.append(b);
        rebuildRows();
        endResetModel();
        emit changed();
        for (int row = 0; row < m_rows.size(); ++row) {
            if (m_rows[row] == m_all.size() - 1)
                return row;
        }
        return -1;
    }

    m_all.append(b);
    rebuildRows();
    emit changed();
    for (int row = 0; row < m_rows.size(); ++row) {
        if (m_rows[row] == m_all.size() - 1)
            return row;
    }
    return -1;
}

bool BookmarkModel::removeAt(int row)
{
    if (row < 0 || row >= m_rows.size())
        return false;
    const int idx = m_rows[row];
    beginRemoveRows(QModelIndex(), row, row);
    m_rows.removeAt(row);
    m_all.removeAt(idx);
    // 删除后 m_rows 里的索引需要同步后移
    for (int &r : m_rows) {
        if (r > idx)
            --r;
    }
    endRemoveRows();
    emit changed();
    return true;
}

int BookmarkModel::clearMedia(const QString &media)
{
    if (media.isEmpty())
        return 0;
    int removed = 0;
    beginResetModel();
    for (int i = m_all.size() - 1; i >= 0; --i) {
        if (sameMedia(m_all[i].media, media)) {
            m_all.removeAt(i);
            ++removed;
        }
    }
    rebuildRows();
    endResetModel();
    if (removed > 0)
        emit changed();
    return removed;
}

void BookmarkModel::clearAll()
{
    if (m_all.isEmpty())
        return;
    beginResetModel();
    m_all.clear();
    m_rows.clear();
    endResetModel();
    emit changed();
}

const Bookmark *BookmarkModel::at(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return nullptr;
    const int idx = m_rows[row];
    return idx >= 0 && idx < m_all.size() ? &m_all[idx] : nullptr;
}

int BookmarkModel::countForMedia(const QString &media) const
{
    int n = 0;
    for (const Bookmark &b : m_all) {
        if (sameMedia(b.media, media))
            ++n;
    }
    return n;
}

int BookmarkModel::nextRow(Ms posMs, Ms epsMs) const
{
    for (int row = 0; row < m_rows.size(); ++row) {
        if (m_all[m_rows[row]].timeMs > posMs + epsMs)
            return row;
    }
    return -1;
}

int BookmarkModel::prevRow(Ms posMs, Ms epsMs) const
{
    for (int row = m_rows.size() - 1; row >= 0; --row) {
        if (m_all[m_rows[row]].timeMs < posMs - epsMs)
            return row;
    }
    return -1;
}

bool BookmarkModel::loadFrom(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    f.close();
    if (perr.error != QJsonParseError::NoError || !doc.isArray())
        return false;

    beginResetModel();
    m_all.clear();
    for (const QJsonValue &v : doc.array()) {
        const QJsonObject o = v.toObject();
        Bookmark b;
        b.media = o.value(QStringLiteral("media")).toString().trimmed();
        b.mediaTitle = o.value(QStringLiteral("mediaTitle")).toString();
        b.timeMs = Ms(o.value(QStringLiteral("timeMs")).toDouble());
        b.note = o.value(QStringLiteral("note")).toString();
        b.createdAt = qint64(o.value(QStringLiteral("createdAt")).toDouble());
        if (b.media.isEmpty() || b.timeMs < 0)
            continue; // 脏数据防护
        m_all.append(b);
    }
    rebuildRows();
    endResetModel();
    emit changed();
    return true;
}

bool BookmarkModel::saveTo(const QString &path) const
{
    QJsonArray arr;
    for (const Bookmark &b : m_all) {
        QJsonObject o;
        o.insert(QStringLiteral("media"), b.media);
        o.insert(QStringLiteral("mediaTitle"), b.mediaTitle);
        o.insert(QStringLiteral("timeMs"), double(b.timeMs));
        o.insert(QStringLiteral("note"), b.note);
        o.insert(QStringLiteral("createdAt"), double(b.createdAt));
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
