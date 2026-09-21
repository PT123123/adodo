#include "RepeatSession.h"

namespace adoloop {

RepeatSession::RepeatSession(QObject *parent)
    : QObject(parent)
{
    m_gapTimer.setSingleShot(true);
    connect(&m_gapTimer, &QTimer::timeout, this, [this]() {
        if (m_state != State::Waiting)
            return;
        // 间隔结束：重播本段（复读计数）或前进
        if (m_repeatsDone < m_config.repeatCount) {
            m_repeatsDone++;
            m_state = State::Playing;
            emit stateChanged(m_state);
            emit commandPlayRange(m_rangeStart, m_rangeEnd);
        } else {
            advance();
        }
    });
}

void RepeatSession::setSentences(const QVector<Sentence> *sentences)
{
    m_sentences = sentences;
}

void RepeatSession::startSentence(int index)
{
    stop();
    if (!m_sentences || index < 0 || index >= m_sentences->size())
        return;
    m_mode = RepeatMode::Sentence;
    m_currentIndex = index;
    m_repeatsDone = 0;
    m_state = State::Playing;
    emit stateChanged(m_state);
    emit sentenceChanged(index);
    playCurrentSentence();
}

void RepeatSession::startAbLoop(Ms a, Ms b)
{
    stop();
    if (b <= a)
        return;
    m_mode = RepeatMode::AbLoop;
    m_rangeStart = a;
    m_rangeEnd = b;
    m_repeatsDone = 0;
    m_state = State::Playing;
    emit stateChanged(m_state);
    emit commandPlayRange(a, b);
}

void RepeatSession::startFullScript(int fromIndex)
{
    stop();
    if (!m_sentences || m_sentences->isEmpty())
        return;
    m_mode = RepeatMode::FullScript;
    m_currentIndex = qBound(0, fromIndex, m_sentences->size() - 1);
    m_repeatsDone = 0;
    m_state = State::Playing;
    emit stateChanged(m_state);
    emit sentenceChanged(m_currentIndex);
    playCurrentSentence();
}

void RepeatSession::stop()
{
    m_gapTimer.stop();
    if (m_state != State::Idle) {
        m_state = State::Idle;
        emit stateChanged(m_state);
    }
    m_currentIndex = -1;
}

void RepeatSession::playCurrentSentence()
{
    if (!m_sentences || m_currentIndex < 0 || m_currentIndex >= m_sentences->size())
        return;
    const Sentence &s = m_sentences->at(m_currentIndex);
    m_rangeStart = s.start;
    m_rangeEnd = s.end;
    m_repeatsDone = 0;
    emit commandPlayRange(s.start, s.end);
}

void RepeatSession::tick(Ms pos)
{
    if (m_state != State::Playing)
        return;
    if (pos >= m_rangeEnd) {
        m_gapTimer.stop();
        onRangeReachedEnd();
    }
}

void RepeatSession::onEof()
{
    if (m_state == State::Playing)
        onRangeReachedEnd();
}

void RepeatSession::onRangeReachedEnd()
{
    if (m_state != State::Playing)
        return;
    emit commandPause();

    if (m_mode == RepeatMode::AbLoop) {
        // A-B：间隔后从头重放，直到 stop()
        m_state = State::Waiting;
        emit stateChanged(m_state);
        m_gapTimer.start(m_config.gapMs);
        return;
    }

    // 句/全文模式：重复计数或前进
    if (m_repeatsDone < m_config.repeatCount - 1) {
        m_repeatsDone++;
        m_state = State::Waiting;
        emit stateChanged(m_state);
        m_gapTimer.start(m_config.gapMs);
        return;
    }
    advance();
}

void RepeatSession::advance()
{
    if (!m_sentences || m_sentences->isEmpty()) {
        m_state = State::Finished;
        emit stateChanged(m_state);
        emit finished();
        return;
    }

    const bool fullScript = (m_mode == RepeatMode::FullScript);
    const int next = fullScript ? m_currentIndex + 1 : m_currentIndex;
    if (next >= m_sentences->size() || (!fullScript && !m_config.autoAdvance)) {
        // 停在句尾
        m_state = State::Finished;
        emit stateChanged(m_state);
        emit finished();
        return;
    }

    m_currentIndex = next;
    m_repeatsDone = 0;
    m_state = State::Playing;
    emit stateChanged(m_state);
    emit sentenceChanged(m_currentIndex);
    playCurrentSentence();
}

} // namespace adoloop
