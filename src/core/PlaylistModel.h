#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>

namespace adoloop {

// 播放列表条目（持久化到 <数据目录>/playlist.json）。
// path 为规范化绝对路径（去重键），title 为显示名（缺省取文件名）。
struct PlaylistEntry {
    QString path;
    QString title;
    qint64 addedAt = 0;    // 首次加入时间（epoch ms）
    qint64 durationMs = 0; // 最近一次已知时长（0 = 未知）
};

// 播放列表模型：QAbstractListModel（风格同 study/FlashcardModel、study/VocabularyModel）。
// 只登记本地媒体——在线媒体（yt-dlp 解析出的临时直链）没有可复用的稳定地址，不入列表。
class PlaylistModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role {
        TitleRole = Qt::UserRole + 1,
        PathRole,
        AddedAtRole,
        DurationRole,
    };
    enum { MaxEntries = 500 }; // 超出后淘汰最早加入的条目（防脏数据无限增长）

    explicit PlaylistModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // 管理：同一路径不产生重复项（返回既有行号）；返回 -1 表示路径为空
    int addFile(const QString &path, const QString &title = QString());
    bool removeAt(int row);
    void clear();
    void setDuration(const QString &path, qint64 durationMs);

    const QVector<PlaylistEntry> &entries() const { return m_entries; }
    const PlaylistEntry *at(int row) const;
    QString pathAt(int row) const;
    int indexOfPath(const QString &path) const;

    // 持久化（JSON 数组）
    bool loadFrom(const QString &path);
    bool saveTo(const QString &path) const;

    // 路径规范化 / 比较（Windows 大小写不敏感）
    static QString normalizePath(const QString &path);
    static bool samePath(const QString &a, const QString &b);

signals:
    void changed();

private:
    QVector<PlaylistEntry> m_entries;
};

} // namespace adoloop
