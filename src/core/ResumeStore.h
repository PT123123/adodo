#pragma once

#include "../core/Types.h"

#include <QHash>
#include <QObject>
#include <QString>

namespace adoloop {

// 断点续播：每个媒体文件记录「上次播放到的位置」，持久化到 <数据目录>/resume.json。
// 与 Settings::resumeEnabled() 解耦——开关只控制「是否自动跳转」，记录始终保留。
class ResumeStore : public QObject {
    Q_OBJECT
public:
    struct Entry {
        Ms positionMs = 0;
        Ms durationMs = 0;
        qint64 updatedAt = 0; // epoch ms
    };

    enum : qint64 {
        MinResumeMs = 3000, // 小于此位置视为「还没开始听」，恢复时不跳转
        EofTailMs = 5000,   // 落在结尾此范围内视为「已听完」→ 清除记录
        MaxEntries = 200,   // 记录条数上限（超出淘汰最久未更新的）
    };

    explicit ResumeStore(QObject *parent = nullptr);

    bool contains(const QString &media) const;
    Entry entry(const QString &media) const; // 不存在返回默认（positionMs = 0）
    Ms position(const QString &media) const;
    int count() const { return m_map.size(); }

    // 记录：位置落在结尾（dur - pos <= EofTailMs）→ 清除记录；其余情况更新
    void record(const QString &media, Ms positionMs, Ms durationMs);
    void forget(const QString &media);
    void clear();

    // 持久化（JSON 数组）
    bool loadFrom(const QString &path);
    bool saveTo(const QString &path) const;

signals:
    void changed();

private:
    void prune();

    QHash<QString, Entry> m_map;
};

} // namespace adoloop
