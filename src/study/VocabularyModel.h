#pragma once

#include "../core/Types.h"

#include <QDateTime>
#include <QObject>
#include <QPair>
#include <QString>
#include <QVector>

namespace adoloop {

class ReadingStore;

// 生词本（分级 + SM-2 间隔复习）。
// M12 日语化：补 reading（假名）/ accent（声调）/ pos（词性）/ lang，
// 由查词典（经 core/ReadingStore）或分词引擎提供；无读音时留空（旧数据兼容）。
struct VocabularyEntry {
    QString word;
    QDateTime addedAt;
    int level = 1; // 1..5
    QString reading; // 假名读音（日语；英语留空）
    QString accent;  // 声调（如 "②"）
    QString pos;     // 词性（如 "名词"）
    QString lang;    // 语言代码 "ja" / "en"
    QVector<QPair<QString, QString>> contexts; // {句子, 来源}
    qint64 due = 0;
    double interval = 0.0;
    double ease = 2.5;
    int lapses = 0;

    ReadingInfo readingInfo() const { return ReadingInfo{reading, accent, pos, lang}; }
};

class VocabularyModel : public QObject {
    Q_OBJECT
public:
    explicit VocabularyModel(QObject *parent = nullptr);

    // 读音来源（可选）：设置后添加生词时自动补齐读音（查词典已写入的缓存）
    void setReadingStore(const ReadingStore *store);
    // 给缺读音的条目补读音（读盘后/缓存更新时调用；返回补齐条数）
    int fillMissingReadings();

    // 添加生词（带上下文）；已存在则补上下文并提级。
    // info 缺项时回落到 ReadingStore（M12：查词弹窗「加入生词本」带读音落库）
    void addWord(const QString &word, const QString &sentence, const QString &source,
                 const ReadingInfo &info = ReadingInfo());
    bool contains(const QString &word) const;
    void markLevel(const QString &word, int level);
    void removeWord(const QString &word);

    const VocabularyEntry *entry(const QString &word) const;
    const QVector<VocabularyEntry> &entries() const { return m_entries; }
    QVector<const VocabularyEntry *> dueEntries() const;

    // 复习反馈：quality 0..5 → SM-2 更新
    void review(const QString &word, int quality);

    // 持久化（JSON）
    bool loadFrom(const QString &path);
    bool saveTo(const QString &path) const;

    // SM-2 纯函数（可单测）
    static void applySm2(VocabularyEntry &e, int quality);

signals:
    void changed();

private:
    int indexOf(const QString &word) const;

    const ReadingStore *m_readings = nullptr;
    QVector<VocabularyEntry> m_entries;
};

} // namespace adoloop
