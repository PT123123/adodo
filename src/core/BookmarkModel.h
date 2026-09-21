#pragma once

#include "../core/Types.h"

#include <QAbstractListModel>
#include <QString>
#include <QVector>

namespace adoloop {

// 书签条目：按文件归属（media = 规范化媒体路径；在线媒体用标题）。
struct Bookmark {
    QString media;
    QString mediaTitle;
    Ms timeMs = 0;         // 书签时间点
    QString note;          // 可选备注
    qint64 createdAt = 0;  // epoch ms

    bool hasNote() const { return !note.isEmpty(); }
};

// 书签模型：内部持有全部书签，通过 setMediaFilter() 过滤出「当前文件」的书签供 UI 显示。
// QAbstractListModel 风格同 study/FlashcardModel（beginInsertRows/beginRemoveRows + changed 信号）。
class BookmarkModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role {
        TimeRole = Qt::UserRole + 1,
        TimeTextRole,
        NoteRole,
        MediaRole,
        MediaTitleRole,
    };
    enum {
        MaxPerMedia = 200,   // 单文件书签上限（超出淘汰最早的，防无限增长）
        MergeWindowMs = 500, // 与该窗口内既有书签视为同一处（只更新备注）
    };

    explicit BookmarkModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // 过滤（当前媒体）；空 = 无当前媒体 → 0 行
    void setMediaFilter(const QString &media);
    QString mediaFilter() const { return m_filter; }

    // 新增书签；返回过滤后的行号（-1 = 失败）
    int addBookmark(const QString &media, const QString &mediaTitle, Ms timeMs, const QString &note);
    bool removeAt(int row);                  // row 为过滤后行号
    int clearMedia(const QString &media);    // 返回删除条数
    void clearAll();

    const Bookmark *at(int row) const;       // 过滤后
    int countForMedia(const QString &media) const;
    int total() const { return m_all.size(); }

    // 相对当前位置的下一 / 上一个书签（±eps 容差，避免原地反复命中）；无则 -1
    int nextRow(Ms posMs, Ms epsMs = 1000) const;
    int prevRow(Ms posMs, Ms epsMs = 1500) const;

    // 持久化（JSON 数组）
    bool loadFrom(const QString &path);
    bool saveTo(const QString &path) const;

    static bool sameMedia(const QString &a, const QString &b);

signals:
    void changed();

private:
    void rebuildRows();

    QVector<Bookmark> m_all; // 全量（插入序）
    QVector<int> m_rows;     // 当前媒体行索引（按时间升序）
    QString m_filter;
};

} // namespace adoloop
