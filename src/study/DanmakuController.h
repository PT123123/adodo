#pragma once

#include "../core/Sentence.h"
#include "../core/Types.h"

#include <QObject>
#include <QString>
#include <QVector>

namespace adoloop {

class VocabularyModel;

// 弹幕式生词复习（幕境）：播放中，当前词若是生词，发射弹幕事件。
// 同一词在同一句只发射一次；每句最多 N 条，避免刷屏。
// M12：日语生词带读音时显示为「漢字（かな）」（读音来自生词本条目/读音缓存）。
class DanmakuController : public QObject {
    Q_OBJECT
public:
    struct Item {
        QString word;
        QString reading;  // M12：假名读音（可空 → 只显示原词）
        QString sentence; // 所在句（弹幕显示原文提示）
        int level = 1;    // 生词等级 → 颜色/速度
        bool isNew = true; // 首次出现 vs 复习出现
        // 显示文本：有读音 → 漢字（かな）
        QString display() const;
    };

    explicit DanmakuController(QObject *parent = nullptr);

    void setVocabulary(VocabularyModel *vocab) { m_vocab = vocab; }

    // 播放位置驱动：当前句与上一句变化时扫描生词
    void tick(Ms pos, const QVector<Sentence> &sentences, int *currentSentenceIndex);

    // 手动触发（取词时也可弹幕复习）；reading 为空时回落到生词本里记的读音
    void trigger(const QString &word, const QString &sentence, int level,
                 const QString &reading = QString());

signals:
    void danmakuEmitted(const adoloop::DanmakuController::Item &item);

private:
    VocabularyModel *m_vocab = nullptr;
    int m_lastSentence = -1;
    QVector<QString> m_emittedInSentence; // 本句已发射的生词
    QVector<QString> m_knownHistory;      // 已出现过的生词（第二次出现视为复习）
};

} // namespace adoloop
