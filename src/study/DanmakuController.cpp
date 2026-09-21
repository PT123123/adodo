#include "DanmakuController.h"
#include "VocabularyModel.h"
#include "../core/ReadingStore.h"

namespace adoloop {

namespace {
constexpr int kMaxPerSentence = 4; // 每句最多 4 条弹幕
}

QString DanmakuController::Item::display() const
{
    // M12：日语生词有读音时飘出「漢字（かな）」
    return ReadingStore::displayWithReading(word, reading);
}

DanmakuController::DanmakuController(QObject *parent)
    : QObject(parent)
{
}

void DanmakuController::tick(Ms pos, const QVector<Sentence> &sentences, int *currentSentenceIndex)
{
    if (!m_vocab || sentences.isEmpty())
        return;

    int idx = -1;
    for (int i = 0; i < sentences.size(); ++i) {
        if (pos >= sentences[i].start && pos < sentences[i].end) {
            idx = i;
            break;
        }
    }
    if (currentSentenceIndex)
        *currentSentenceIndex = idx;

    if (idx < 0) {
        m_lastSentence = -1;
        return;
    }
    if (idx == m_lastSentence)
        return; // 同一句不重复扫描

    m_lastSentence = idx;
    m_emittedInSentence.clear();

    const Sentence &s = sentences[idx];
    const QVector<WordToken> ws = s.estimatedWords();
    int emitted = 0;
    for (const WordToken &w : ws) {
        if (emitted >= kMaxPerSentence)
            break;
        const QString clean = w.word.trimmed();
        if (clean.size() < 2)
            continue;
        if (m_vocab->contains(clean)) {
            const VocabularyEntry *e = m_vocab->entry(clean);
            const bool isNew = !m_knownHistory.contains(clean);
            Item it;
            it.word = clean;
            it.reading = e ? e->reading : QString();
            it.sentence = s.text;
            it.level = e ? e->level : 1;
            it.isNew = isNew;
            m_emittedInSentence << clean;
            if (isNew)
                m_knownHistory << clean;
            emit danmakuEmitted(it);
            ++emitted;
        }
    }
}

void DanmakuController::trigger(const QString &word, const QString &sentence, int level,
                                const QString &reading)
{
    Item it;
    it.word = word;
    it.reading = reading;
    if (it.reading.isEmpty() && m_vocab) {
        if (const VocabularyEntry *e = m_vocab->entry(word))
            it.reading = e->reading;
    }
    it.sentence = sentence;
    it.level = level;
    it.isNew = !m_knownHistory.contains(word);
    if (it.isNew)
        m_knownHistory << word;
    emit danmakuEmitted(it);
}

} // namespace adoloop
