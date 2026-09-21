#include "SentenceBuilder.h"
#include "../core/Tokenizer.h"
#include "../util/StringUtil.h"

#include <QRandomGenerator>

namespace adoloop {

void SentenceBuilder::load(const QString &sentence)
{
    m_expected = sentence.trimmed();
    reset();
}

void SentenceBuilder::reset()
{
    // 语言感知分词：日语按分词器切 chip（标点并入前一词），英语同 M10 前
    m_source = Tokenizer::splitChips(m_expected, m_lang);
    m_answer.clear();
    // Fisher-Yates 洗牌
    QRandomGenerator *rng = QRandomGenerator::global();
    for (int i = m_source.size() - 1; i > 0; --i) {
        const int j = int(rng->bounded(quint32(i + 1)));
        m_source.swapItemsAt(i, j);
    }
}

void SentenceBuilder::pick(int sourceIndex)
{
    if (sourceIndex < 0 || sourceIndex >= m_source.size())
        return;
    m_answer << m_source.takeAt(sourceIndex);
}

void SentenceBuilder::unpick(int answerIndex)
{
    if (answerIndex < 0 || answerIndex >= m_answer.size())
        return;
    m_source << m_answer.takeAt(answerIndex);
}

bool SentenceBuilder::check() const
{
    // 忽略连续空白差异
    const QString joined = m_answer.join(separator()).simplified();
    const QString expected = m_expected.simplified();
    if (joined.compare(expected, Qt::CaseSensitive) == 0)
        return true;
    // 容错：词序相同但标点/空格细节不同
    return strutil::normalizeForCheck(joined, true, true)
        == strutil::normalizeForCheck(expected, true, true);
}

} // namespace adoloop
