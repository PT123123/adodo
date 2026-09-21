#include "ShadowingSession.h"
#include "../core/AudioAnalysis.h"

namespace adoloop {

namespace {
constexpr int kVoiceWaitMs = 15000;  // 声控等待语音最长 15s
constexpr int kSilenceTailMs = 1500; // 语音尾部静音 1.5s 判定结束
constexpr int kMaxRecordMs = 20000;  // 单次跟读最长 20s
}

ShadowingSession::ShadowingSession(QObject *parent)
    : QObject(parent)
{
    m_voiceTimer.setSingleShot(true);
    m_silenceTimer.setSingleShot(true);

    connect(&m_voiceTimer, &QTimer::timeout, this, [this]() {
        if (m_state == State::ListeningVoice) {
            emit commandCancelBufferRecording();
            emit resultReady(Result{m_currentIndex, {}, QString()});
            setState(State::Finished);
        }
    });
    connect(&m_silenceTimer, &QTimer::timeout, this, [this]() {
        if (m_state == State::Recording) {
            emit commandStopBufferRecording();
            // 数据将由 AppController 回传 onRecordingData
        }
    });
}

void ShadowingSession::setSentences(const QVector<Sentence> *sentences)
{
    m_sentences = sentences;
}

void ShadowingSession::begin(int sentenceIndex, ShadowingMode mode)
{
    stop();
    if (!m_sentences || sentenceIndex < 0 || sentenceIndex >= m_sentences->size())
        return;
    m_currentIndex = sentenceIndex;
    m_mode = mode;
    const Sentence &s = m_sentences->at(sentenceIndex);
    m_modelStart = s.start;
    m_modelEnd = s.end;
    m_voiceOnset = false;

    setState(State::PlayingModel);
    emit commandPlaySentence(sentenceIndex);
}

void ShadowingSession::stop()
{
    m_voiceTimer.stop();
    m_silenceTimer.stop();
    if (m_state != State::Idle)
        emit commandCancelBufferRecording();
    if (m_state != State::Idle && m_state != State::Finished) {
        setState(State::Idle);
    } else if (m_state == State::Idle) {
        // 已空闲
    }
    m_currentIndex = -1;
}

void ShadowingSession::tick(Ms pos)
{
    if (m_state != State::PlayingModel)
        return;
    if (pos >= m_modelEnd)
        onModelFinished();
}

void ShadowingSession::onModelFinished()
{
    if (m_state != State::PlayingModel)
        return;
    emit commandPause();

    switch (m_mode) {
    case ShadowingMode::Manual:
        // 停止，等待用户点击
        setState(State::ListeningVoice); // 复用为「等待开始」状态
        m_voiceTimer.start(kVoiceWaitMs);
        break;
    case ShadowingMode::VoiceTriggered:
        setState(State::ListeningVoice);
        emit commandStartBufferRecording(); // 先录着，检测到语音才算正式
        m_voiceTimer.start(kVoiceWaitMs);
        break;
    case ShadowingMode::Crazy:
        // 不等待：直接比对上一段（无录音则记 0 分）并自动下一句
        emit resultReady(Result{m_currentIndex, {}, QString()});
        emit commandAutoAdvance();
        setState(State::Finished);
        break;
    }
}

void ShadowingSession::onUserStart()
{
    if (m_state != State::ListeningVoice)
        return;
    m_voiceTimer.stop();
    setState(State::Recording);
    emit commandStartBufferRecording();
}

void ShadowingSession::onUserStop()
{
    if (m_state != State::Recording)
        return;
    m_silenceTimer.stop();
    emit commandStopBufferRecording();
}

void ShadowingSession::feedVoiceDetected(bool voice)
{
    if (m_state == State::ListeningVoice && m_mode == ShadowingMode::VoiceTriggered) {
        if (voice && !m_voiceOnset) {
            m_voiceOnset = true;
            m_voiceTimer.stop();
            // 已在缓冲录音中：标记正式录音开始
            setState(State::Recording);
            // 之后由 AppController 持续 feedVoiceDetected 判断尾部静音
        }
        return;
    }
    if (m_state == State::Recording && m_mode == ShadowingMode::VoiceTriggered) {
        if (voice) {
            m_silenceTimer.stop();
        } else if (!m_silenceTimer.isActive()) {
            m_silenceTimer.start(kSilenceTailMs);
        }
    }
}

void ShadowingSession::onRecordingData(const QVector<float> &userPcm, int userSr,
                                       const QVector<float> &modelPcm, int modelSr)
{
    if (m_state != State::Recording)
        return;
    ShadowingScore score = AudioAnalysis::compare(modelPcm, modelSr, userPcm, userSr);
    if (score.score <= 0) {
        score.detail = QStringLiteral("未检测到有效跟读语音");
    }
    Result r;
    r.sentenceIndex = m_currentIndex;
    r.score = score;
    emit resultReady(r);
    setState(State::Finished);
}

void ShadowingSession::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged(m_state);
}

void ShadowingSession::finishCurrent(bool)
{
    // 预留：清理句级状态
}

} // namespace adoloop
