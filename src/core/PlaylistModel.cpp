#include "PlaylistModel.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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

PlaylistModel::PlaylistModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int PlaylistModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant PlaylistModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};
    const PlaylistEntry &e = m_entries[index.row()];
    switch (role) {
    case TitleRole:
        return e.title;
    case PathRole:
        return e.path;
    case AddedAtRole:
        return e.addedAt;
    case DurationRole:
        return e.durationMs;
    default:
        return {};
    }
}

QHash<int, QByteArray> PlaylistModel::roleNames() const
{
    return {
        {TitleRole, "title"},
        {PathRole, "path"},
        {AddedAtRole, "addedAt"},
        {DurationRole, "duration"},
    };
}

QString PlaylistModel::normalizePath(const QString &path)
{
    const QString t = path.trimmed();
    if (t.isEmpty())
        return {};
    // 相对路径按当前工作目录补全；不解析符号链接（保持用户可见路径）
    return QDir::cleanPath(QFileInfo(t).absoluteFilePath());
}

bool PlaylistModel::samePath(const QString &a, const QString &b)
{
    const QString ka = normalizePath(a);
    const QString kb = normalizePath(b);
    if (ka.isEmpty() || kb.isEmpty())
        return false;
    return sameKey(ka, kb);
}

int PlaylistModel::indexOfPath(const QString &path) const
{
    const QString key = normalizePath(path);
    if (key.isEmpty())
        return -1;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (sameKey(m_entries[i].path, key))
            return i;
    }
    return -1;
}

int PlaylistModel::addFile(const QString &path, const QString &title)
{
    const QString key = normalizePath(path);
    if (key.isEmpty())
        return -1;
    const int existing = indexOfPath(key);
    if (existing >= 0)
        return existing; // 去重：不重复插入，也不打乱既有顺序

    if (m_entries.size() >= MaxEntries) {
        int oldest = 0;
        for (int i = 1; i < m_entries.size(); ++i) {
            if (m_entries[i].addedAt < m_entries[oldest].addedAt)
                oldest = i;
        }
        beginRemoveRows(QModelIndex(), oldest, oldest);
        m_entries.removeAt(oldest);
        endRemoveRows();
    }

    PlaylistEntry e;
    e.path = key;
    e.title = title.trimmed().isEmpty() ? QFileInfo(key).fileName() : title.trimmed();
    e.addedAt = QDateTime::currentMSecsSinceEpoch();

    beginInsertRows(QModelIndex(), m_entries.size(), m_entries.size());
    m_entries.append(e);
    endInsertRows();
    emit changed();
    return m_entries.size() - 1;
}

bool PlaylistModel::removeAt(int row)
{
    if (row < 0 || row >= m_entries.size())
        return false;
    beginRemoveRows(QModelIndex(), row, row);
    m_entries.removeAt(row);
    endRemoveRows();
    emit changed();
    return true;
}

void PlaylistModel::clear()
{
    if (m_entries.isEmpty())
        return;
    beginResetModel();
    m_entries.clear();
    endResetModel();
    emit changed();
}

void PlaylistModel::setDuration(const QString &path, qint64 durationMs)
{
    const int row = indexOfPath(path);
    if (row < 0 || m_entries[row].durationMs == durationMs)
        return;
    m_entries[row].durationMs = durationMs;
    emit dataChanged(index(row), index(row), {DurationRole});
    emit changed();
}

const PlaylistEntry *PlaylistModel::at(int row) const
{
    return row >= 0 && row < m_entries.size() ? &m_entries[row] : nullptr;
}

QString PlaylistModel::pathAt(int row) const
{
    return row >= 0 && row < m_entries.size() ? m_entries[row].path : QString();
}

bool PlaylistModel::loadFrom(const QString &path)
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
    m_entries.clear();
    for (const QJsonValue &v : doc.array()) {
        const QJsonObject o = v.toObject();
        const QString p = normalizePath(o.value(QStringLiteral("path")).toString());
        if (p.isEmpty() || indexOfPath(p) >= 0)
            continue; // 脏数据防护：空路径与重复路径直接丢弃
        PlaylistEntry e;
        e.path = p;
        e.title = o.value(QStringLiteral("title")).toString();
        if (e.title.isEmpty())
            e.title = QFileInfo(p).fileName();
        e.addedAt = qint64(o.value(QStringLiteral("addedAt")).toDouble());
        e.durationMs = qint64(o.value(QStringLiteral("durationMs")).toDouble());
        m_entries.append(e);
    }
    endResetModel();
    emit changed();
    return true;
}

bool PlaylistModel::saveTo(const QString &path) const
{
    QJsonArray arr;
    for (const PlaylistEntry &e : m_entries) {
        QJsonObject o;
        o.insert(QStringLiteral("path"), e.path);
        o.insert(QStringLiteral("title"), e.title);
        o.insert(QStringLiteral("addedAt"), double(e.addedAt));
        o.insert(QStringLiteral("durationMs"), double(e.durationMs));
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
