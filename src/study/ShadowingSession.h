#pragma once

#include "../core/Sentence.h"
#include "../core/Types.h"

#include <QObject>
#include <QTimer>
#include <QVector>

namespace adoloop {

// 随意读（跟读）会话：播放模型句 → 录音 → 比对评分。
// 三种方式（Aboboo）：
//   手动：播放原音后停止，用户点击录音按钮开始
//   声控：检测到用户声音自动开始录音（VAD 轮询经 feedVoiceDetected 反馈）
//   疯狂：连续快速跟读（播放结束立即进入下一句，弱化比对等待）
// 与引擎/录音解耦：全部经命令信号由 AppController 执行。
class ShadowingSession : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, PlayingModel, ListeningVoice, Recording, Comparing, Finished };
    Q_ENUM(State)

    struct Result {
        int sentenceIndex = -1;
        ShadowingScore score;
        QString recordingPath; // 已保存的录音（空表示未保存）
    };

    explicit ShadowingSession(QObject *parent = nullptr);

    void setSentences(const QVector<Sentence> *sentences);

    void begin(int sentenceIndex, ShadowingMode mode);
    void stop();

    // 播放模型句时的位置事件
    void tick(Ms pos);
    // 模型句播放完毕（由 AppController 判断区间结束）
    void onModelFinished();

    // 手动模式：用户操作
    void onUserStart();
    void onUserStop();

    // 声控模式：VAD 轮询反馈（AppController 每 100ms 调用）
    void feedVoiceDetected(bool voice);

    // 录音数据就绪（AppController 停止缓冲录音后回传；modelPcm 可为空）
    void onRecordingData(const QVector<float> &userPcm, int userSr,
                         const QVector<float> &modelPcm, int modelSr);

    State state() const { return m_state; }
    ShadowingMode mode() const { return m_mode; }
    int currentIndex() const { return m_currentIndex; }

signals:
    void stateChanged(ShadowingSession::State state);
    void commandPlaySentence(int index);
    void commandPause();
    void commandStartBufferRecording();
    void commandStopBufferRecording();
    void commandCancelBufferRecording();
    void commandAutoAdvance(); // 疯狂模式：立即下一句
    void resultReady(const ShadowingSession::Result &result);

private:
    void setState(State s);
    void finishCurrent(bool ok);

    State m_state = State::Idle;
    ShadowingMode m_mode = ShadowingMode::Manual;
    const QVector<Sentence> *m_sentences = nullptr;
    int m_currentIndex = -1;
    Ms m_modelStart = 0;
    Ms m_modelEnd = 0;
    bool m_voiceOnset = false;
    QTimer m_voiceTimer;      // 声控：等待超时
    QTimer m_silenceTimer;    // 声控：尾静音判定
};

} // namespace adoloop
