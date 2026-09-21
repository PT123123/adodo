#pragma once

#include "../core/Sentence.h"
#include "../core/Types.h"

#include <QObject>
#include <QTimer>
#include <QVector>

namespace adoloop {

// 复读会话（逐句 / A-B 区间 / 全文方案），有穷状态机。
// 与播放引擎解耦：通过命令信号驱动，由 AppController 回灌位置/结束事件。
class RepeatSession : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Playing, Waiting, Repeating, Finished };
    Q_ENUM(State)

    struct Config {
        int repeatCount = 1;   // 每句重复次数（含首次）
        int gapMs = 500;       // 句间/循环间隔
        bool autoAdvance = true; // 句末自动进入下一句
    };

    explicit RepeatSession(QObject *parent = nullptr);

    void setSentences(const QVector<Sentence> *sentences); // 弱引用，外部持有
    void setConfig(const Config &c) { m_config = c; }
    const Config &config() const { return m_config; }

    void startSentence(int index);
    void startAbLoop(Ms a, Ms b);
    void startFullScript(int fromIndex = 0);
    void stop();

    State state() const { return m_state; }
    int currentIndex() const { return m_currentIndex; }
    RepeatMode mode() const { return m_mode; }

    // 由播放位置事件驱动
    void tick(Ms pos);
    // 由播放结束事件驱动
    void onEof();

signals:
    void stateChanged(RepeatSession::State state);
    void commandPlayRange(adoloop::Ms start, adoloop::Ms end); // 引擎播放区间
    void commandSeek(adoloop::Ms pos);
    void commandPause();
    void sentenceChanged(int index);
    void finished();

private:
    void playCurrentSentence();
    void onRangeReachedEnd();
    void advance();

    State m_state = State::Idle;
    RepeatMode m_mode = RepeatMode::Sentence;
    const QVector<Sentence> *m_sentences = nullptr;
    Config m_config;
    int m_currentIndex = -1;
    Ms m_rangeStart = 0;
    Ms m_rangeEnd = 0;
    int m_repeatsDone = 0;
    QTimer m_gapTimer;
};

} // namespace adoloop
