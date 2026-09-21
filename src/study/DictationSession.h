#pragma once

#include "../core/Sentence.h"
#include "../core/Types.h"

#include <QObject>
#include <QSet>
#include <QStringList>
#include <QVector>

namespace adoloop {

// 听写会话：抠词 / 单句 / 自由 三种模式，实时纠错，产出汇报。
// - 抠词：播放一句 → 用户输词自动归位（可不按顺序）；未填词自动复读词区间。
// - 单句：播放一句 → 整句输入，回车判定。
// - 自由：连续播放 → 分段输入，每段汇报。
class DictationSession : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Playing, Inputting, Finished };
    Q_ENUM(State)

    struct Item {
        QString expected; // 期望文本（句）
        QString typed;
        bool correct = false;
    };

    struct Report {
        QString media;
        QString modeName;
        QVector<Item> items;
        int total = 0;
        int correct = 0;
        double accuracy = 0.0;
        QStringList missedWords;
        QString toJson() const;
    };

    explicit DictationSession(QObject *parent = nullptr);

    void setMediaTitle(const QString &title);

    void begin(DictationMode mode, const QVector<Sentence> &sentences,
               int startIndex = 0, bool ignorePunct = true, bool ignoreCase = true);
    void stop();

    // 学习语言（缺省从句子的 lang 推断；app 层可显式设置）
    void setLang(Lang lang) { m_lang = lang; }
    Lang lang() const { return m_lang; }

    State state() const { return m_state; }
    DictationMode mode() const { return m_mode; }
    int currentIndex() const { return m_currentIndex; }

    // 用户输入（抠词模式：单个词；单句/自由：整句，回车触发判定）
    void inputText(const QString &text);
    void reveal(); // 显示答案，结束本项
    void next();   // 手动进入下一句

    // 抠词模式状态查询
    const QVector<bool> &slotFilled() const { return m_slotsFilled; }
    const QStringList &slotWords() const { return m_slotWords; }

    // 播放事件
    void onTick(Ms pos);

signals:
    void stateChanged(DictationSession::State state);
    void commandPlayRange(adoloop::Ms start, adoloop::Ms end);
    void commandPause();
    void commandRepeatWord(int wordIndex); // 抠词：复读单词区间
    void wordChecked(int wordIndex, bool correct);
    void sentenceChecked(bool correct);
    void reportReady(const DictationSession::Report &report);

private:
    void startCurrent();
    void advanceSentence();
    void finishSession();
    void emitCommandRepeatNextUnfilled();
    void setState(State s);
    Report buildReport() const;
    bool checkWord(int slot, const QString &typed) const;

    State m_state = State::Idle;
    DictationMode m_mode = DictationMode::Word;
    QVector<Sentence> m_sentences;
    int m_currentIndex = -1;
    bool m_ignorePunct = true;
    bool m_ignoreCase = true;
    Lang m_lang = Lang::Japanese; // 分词语言（begin 时从句子推断）
    QString m_mediaTitle;
    int m_sentenceAttempts = 0;

    QStringList m_slotWords;
    QVector<bool> m_slotsFilled;

    QString m_currentExpected;
    QString m_currentTyped;
    QVector<Item> m_items;
    Ms m_segStart = 0;
    Ms m_segEnd = 0;
};

} // namespace adoloop
