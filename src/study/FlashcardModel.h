#pragma once

#include "../core/Sentence.h"
#include "../core/Types.h"

#include <QAbstractListModel>
#include <QDateTime>
#include <QVector>

namespace adoloop {

class ReadingStore;

// 原句闪卡（Echo-Loop）：挖空句 → 答案词，SM-2 间隔复习。
// 同时是 QAbstractListModel 供 UI 直接展示（待复习队列）。
// M12：卡片补 reading（答案词的假名读音，来自 core/ReadingStore），
// 旧 flashcards.json 无该字段时留空（解析不失败）。
struct Flashcard {
    QString prompt;    // 挖空句
    QString answer;    // 被挖词/短语
    QString reading;   // 答案词读音（假名；M12，可空）
    QString source;    // 出处（句子）
    QString media;     // 媒体标题
    int sentenceId = -1;
    qint64 due = 0;    // epoch ms
    double interval = 0.0;
    double ease = 2.5;
    int lapses = 0;
};

class FlashcardModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role {
        PromptRole = Qt::UserRole + 1,
        AnswerRole,
        SourceRole,
        MediaRole,
        DueRole,
        ReadingRole, // M12
    };

    explicit FlashcardModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // 读音来源（可选；M12）：新卡自动带读音，读盘后也会给缺读音的旧卡补齐
    void setReadingStore(const ReadingStore *store);
    // 从读音缓存补齐所有缺读音的卡片（loadFrom 之后调用；返回补齐张数）
    int fillMissingReadings();

    // 管理
    void addCard(const Flashcard &card);
    void addClozeCardsFromSentence(const Sentence &s, const QString &mediaTitle, int count = 2);
    void removeCard(int row);
    void clear();

    // 复习：quality 0..5（SM-2）
    void rate(int row, int quality);

    const QVector<Flashcard> &cards() const { return m_cards; }
    const Flashcard *cardAt(int row) const { return row >= 0 && row < m_cards.size() ? &m_cards[row] : nullptr; }

    // 持久化（JSON）
    bool loadFrom(const QString &path);
    bool saveTo(const QString &path) const;

    // SM-2 纯函数（可单测）
    static void applySm2(Flashcard &card, int quality);

    // 待复习卡片（due <= now）
    QVector<int> dueRows(qint64 nowMs = QDateTime::currentMSecsSinceEpoch()) const;

    // 揭晓/列表用的答案文本（M12）：「漢字（かな）」，无读音时原样返回 answer
    static QString answerWithReading(const Flashcard &card);

signals:
    void changed();

private:
    const ReadingStore *m_readings = nullptr;
    QVector<Flashcard> m_cards;
};

} // namespace adoloop
