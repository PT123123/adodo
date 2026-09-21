#include "DictationSession.h"
#include "../core/Tokenizer.h"
#include "../util/StringUtil.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace adoloop {

namespace {
constexpr int kMaxWordAttempts = 3; // 同一句最多重练次数
}

QString DictationSession::Report::toJson() const
{
    QJsonObject root;
    root.insert(QStringLiteral("media"), media);
    root.insert(QStringLiteral("mode"), modeName);
    root.insert(QStringLiteral("total"), total);
    root.insert(QStringLiteral("correct"), correct);
    root.insert(QStringLiteral("accuracy"), accuracy);
    QJsonArray arr;
    for (const Item &it : items) {
        QJsonObject o;
        o.insert(QStringLiteral("expected"), it.expected);
        o.insert(QStringLiteral("typed"), it.typed);
        o.insert(QStringLiteral("correct"), it.correct);
        arr.append(o);
    }
    root.insert(QStringLiteral("items"), arr);
    root.insert(QStringLiteral("missed"), QJsonArray::fromStringList(missedWords));
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

DictationSession::DictationSession(QObject *parent)
    : QObject(parent)
{
}

void DictationSession::setMediaTitle(const QString &title)
{
    m_mediaTitle = title;
}

void DictationSession::begin(DictationMode mode, const QVector<Sentence> &sentences,
                             int startIndex, bool ignorePunct, bool ignoreCase)
{
    stop();
    if (sentences.isEmpty())
        return;
    m_mode = mode;
    m_sentences = sentences;
    m_currentIndex = qBound(0, startIndex, sentences.size() - 1);
    m_lang = sentences.at(m_currentIndex).lang; // 分词语言跟随句子（app 层已注入）
    m_ignorePunct = ignorePunct;
    m_ignoreCase = ignoreCase;
    m_items.clear();
    m_sentenceAttempts = 0;

    setState(State::Playing);
    startCurrent();
}

void DictationSession::stop()
{
    if (m_state == State::Idle)
        return;
    m_state = State::Idle;
    emit stateChanged(m_state);
}

void DictationSession::startCurrent()
{
    if (m_currentIndex < 0 || m_currentIndex >= m_sentences.size()) {
        finishSession();
        return;
    }
    const Sentence &s = m_sentences[m_currentIndex];
    m_segStart = s.start;
    m_segEnd = s.end;
    m_currentExpected = s.text;
    m_currentTyped.clear();

    if (m_mode == DictationMode::Word) {
        m_slotWords = s.wordList();
        m_slotsFilled = QVector<bool>(m_slotWords.size(), false);
        emit commandPlayRange(s.start, s.end);
    } else {
        emit commandPlayRange(s.start, m_mode == DictationMode::Free
                                           ? m_sentences.last().end
                                           : s.end);
    }
}

void DictationSession::inputText(const QString &text)
{
    if (m_state != State::Playing && m_state != State::Inputting)
        return;

    if (m_mode == DictationMode::Word) {
        const QString word = text.trimmed();
        if (word.isEmpty())
            return;
        // 抠词：不按顺序，找第一个未填且匹配的槽
        for (int i = 0; i < m_slotWords.size(); ++i) {
            if (m_slotsFilled[i])
                continue;
            if (checkWord(i, word)) {
                m_slotsFilled[i] = true;
                emit wordChecked(i, true);
                // 全部填完 → 本句完成
                bool allFilled = true;
                for (bool f : m_slotsFilled)
                    allFilled = allFilled && f;
                if (allFilled) {
                    Item it;
                    it.expected = m_currentExpected;
                    it.typed = m_slotWords.join(' ');
                    it.correct = true;
                    m_items << it;
                    emit sentenceChecked(true);
                    emit commandPause();
                    advanceSentence();
                } else {
                    emitCommandRepeatNextUnfilled();
                }
                return;
            }
        }
        // 无匹配：反馈错误（UI 红闪），并自动复读当前未填词区间
        emit wordChecked(-1, false);
        emitCommandRepeatNextUnfilled();
        return;
    }

    // 单句/自由：整句输入，回车或句号触发判定
    m_currentTyped = text;
    if (text.endsWith('\n')) {
        const QString typed = text.left(text.size() - 1).trimmed();
        const bool ok = strutil::normalizeForCheck(m_currentExpected, m_ignorePunct, m_ignoreCase)
            == strutil::normalizeForCheck(typed, m_ignorePunct, m_ignoreCase);
        emit commandPause();
        Item it;
        it.expected = m_currentExpected;
        it.typed = typed;
        it.correct = ok;
        m_items << it;
        emit sentenceChecked(ok);
        if (m_mode == DictationMode::Sentence) {
            setState(State::Finished);
            emit reportReady(buildReport());
            setState(State::Idle);
        } else {
            // 自由听写：继续下一句（用户 next() 或自动）
            emit reportReady(buildReport()); // 阶段性汇报，可继续
            setState(State::Finished);
        }
        return;
    }
}

void DictationSession::reveal()
{
    if (m_state == State::Idle)
        return;
    if (m_mode == DictationMode::Word) {
        // 揭示答案：未填槽记为错误
        Item it;
        it.expected = m_currentExpected;
        QStringList typedParts;
        for (int i = 0; i < m_slotWords.size(); ++i)
            typedParts << (m_slotsFilled[i] ? m_slotWords[i] : QStringLiteral("___"));
        it.typed = typedParts.join(' ');
        it.correct = false;
        m_items << it;
        emit sentenceChecked(false);
        emit commandPause();
        advanceSentence();
        return;
    }
    emit commandPause();
    Item it;
    it.expected = m_currentExpected;
    it.typed = m_currentTyped;
    it.correct = false;
    m_items << it;
    emit sentenceChecked(false);
    if (m_mode == DictationMode::Sentence) {
        setState(State::Finished);
        emit reportReady(buildReport());
        setState(State::Idle);
    } else {
        setState(State::Finished);
    }
}

void DictationSession::next()
{
    if (m_state == State::Idle)
        return;
    if (m_mode == DictationMode::Word) {
        bool anyUnfilled = false;
        for (bool f : m_slotsFilled)
            anyUnfilled = anyUnfilled || !f;
        if (anyUnfilled) {
            reveal(); // 未完成跳过：本句记错
            return;
        }
    }
    advanceSentence();
}

void DictationSession::advanceSentence()
{
    m_currentIndex++;
    m_sentenceAttempts = 0;
    if (m_currentIndex >= m_sentences.size()) {
        finishSession();
        return;
    }
    setState(State::Playing);
    startCurrent();
}

void DictationSession::finishSession()
{
    emit commandPause();
    setState(State::Finished);
    emit reportReady(buildReport());
    setState(State::Idle);
}

void DictationSession::emitCommandRepeatNextUnfilled()
{
    int next = -1;
    for (int j = 0; j < m_slotWords.size(); ++j) {
        if (!m_slotsFilled[j]) {
            next = j;
            break;
        }
    }
    if (next >= 0)
        emit commandRepeatWord(next);
}

bool DictationSession::checkWord(int slot, const QString &typed) const
{
    if (slot < 0 || slot >= m_slotWords.size())
        return false;
    return strutil::normalizeForCheck(m_slotWords[slot], m_ignorePunct, m_ignoreCase)
        == strutil::normalizeForCheck(typed, m_ignorePunct, m_ignoreCase);
}

DictationSession::Report DictationSession::buildReport() const
{
    Report r;
    r.media = m_mediaTitle;
    r.modeName = m_mode == DictationMode::Word ? QStringLiteral("抠词听写")
        : m_mode == DictationMode::Sentence ? QStringLiteral("单句听写")
                                            : QStringLiteral("自由听写");
    r.items = m_items;
    r.total = m_items.size();
    for (const Item &it : m_items)
        if (it.correct)
            r.correct++;
    r.accuracy = r.total > 0 ? double(r.correct) / r.total : 0.0;

    QSet<QString> missed;
    for (const Item &it : m_items) {
        if (!it.correct) {
            const QStringList ws = Tokenizer::tokenize(it.expected, m_lang); // 语言感知（日语分词）
            for (const QString &w : ws) {
                if (!strutil::normalizeForCheck(it.typed, true, true)
                         .contains(strutil::normalizeForCheck(w, true, true)))
                    missed << w;
            }
        }
    }
    r.missedWords = missed.values();
    return r;
}

void DictationSession::onTick(Ms pos)
{
    if (m_state != State::Playing)
        return;
    if (m_mode != DictationMode::Free && pos >= m_segEnd) {
        // 句末暂停等输入
        emit commandPause();
        setState(State::Inputting);
    }
}

void DictationSession::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged(m_state);
}

} // namespace adoloop
