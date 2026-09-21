#pragma once

#include "Tokenizer.h"
#include "Types.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

namespace adoloop {

// 读音缓存（M12）：把「查词典 / 分词得到的读音」持久化下来，供离线复用。
//
// 为什么需要：内置日语分词是启发式的，**给不出读音**（本机也没有 MeCab），
// 于是「查过词典的词」是唯一的读音来源；M11 把它只显示在弹窗里，用完即弃。
// 本类把它落到 <数据目录>/readings.json，字幕悬停提示、生词本、闪卡、弹幕
// 都能直接拿到假名/声调/词性，不需要重新联网。
//
// 写入优先级（高 → 低）：
//   ① 查词成功（putFromDict）：权威来源，覆盖旧值；
//   ② 分词引擎的 Token::reading（putFromTokens，MeCab / Python 桥档才有）：只在缺失时补。
// 查词失败（DictEntry::ok() == false）或读音为空时**不写入**，避免污染缓存。
//
// 分层约束：属 core 层，不依赖 app/Settings——持久化路径由 app 层注入
// （与 PlaylistModel / BookmarkModel / ResumeStore 既有做法一致）。
class ReadingStore : public QObject {
    Q_OBJECT
public:
    struct Entry {
        QString word;     // 表记（原样保存，便于显示）
        ReadingInfo info; // 读音 / 声调 / 词性 / 语言
        qint64 updatedAt = 0;
    };

    explicit ReadingStore(QObject *parent = nullptr);

    // ---------- 写入 ----------
    // 查词成功后写入（要求 entry.ok() 且读音非空）；返回是否真的写入
    bool putFromDict(const DictEntry &entry, Lang lang);
    // 单条写入（读音为空则丢弃）；overwrite=false 时只在「尚无读音」时补
    bool putReading(const QString &word, const QString &reading,
                    const QString &accent = QString(), const QString &pos = QString(),
                    Lang lang = Lang::Japanese, bool overwrite = true);
    // 分词结果里的读音（MeCab / Python 桥档用）；默认不覆盖已有值（词典优先）
    int putFromTokens(const QVector<Token> &tokens, Lang lang, bool overwrite = false);
    // 整条写入（从磁盘读回时也用这个入口）
    bool put(const Entry &entry, bool overwrite = true);

    // ---------- 查询（不需要联网） ----------
    bool contains(const QString &word) const;
    const Entry *find(const QString &word) const;
    QString reading(const QString &word) const;
    QString accent(const QString &word) const;
    QString pos(const QString &word) const;
    ReadingInfo info(const QString &word) const;
    int size() const { return m_map.size(); }
    const QHash<QString, Entry> &entries() const { return m_map; }
    void clear();

    // ---------- 展示辅助（纯函数，可离线单测） ----------
    // 「漢字（かな）」（弹幕/闪卡用）；reading 为空时原样返回 word
    static QString displayWithReading(const QString &word, const QString &reading);
    // 生词本一行：「漢字 [かな] ② 名词」（缺项自动省略）；无读音时返回 word
    static QString displayWithInfo(const QString &word, const ReadingInfo &info);

    // ---------- 持久化（JSON 数组） ----------
    bool loadFrom(const QString &path);
    bool saveTo(const QString &path) const;

    // 归一化键：去空白 + 大小写不敏感（日语无大小写，英语词条同样适用）
    static QString keyOf(const QString &word);

    // 上限（防脏数据无限增长）：超出时淘汰最久未更新的条目
    static int maxEntries();

signals:
    void changed();

private:
    void evictIfNeeded();

    QHash<QString, Entry> m_map;
};

} // namespace adoloop
