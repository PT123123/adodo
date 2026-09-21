#include "FlashcardModel.h"
#include "../core/ReadingStore.h"
#include "../core/Tokenizer.h"
#include "../util/StringUtil.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace adoloop {

FlashcardModel::FlashcardModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int FlashcardModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_cards.size();
}

QVariant FlashcardModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_cards.size())
        return {};
    const Flashcard &c = m_cards[index.row()];
    switch (role) {
    case PromptRole:
        return c.prompt;
    case AnswerRole:
        return c.answer;
    case SourceRole:
        return c.source;
    case MediaRole:
        return c.media;
    case DueRole:
        return c.due;
    case ReadingRole:
        return c.reading;
    default:
        return {};
    }
}

QHash<int, QByteArray> FlashcardModel::roleNames() const
{
    return {
        {PromptRole, "prompt"},
        {AnswerRole, "answer"},
        {SourceRole, "source"},
        {MediaRole, "media"},
        {DueRole, "due"},
        {ReadingRole, "reading"},
    };
}

void FlashcardModel::setReadingStore(const ReadingStore *store)
{
    m_readings = store;
    fillMissingReadings();
}

int FlashcardModel::fillMissingReadings()
{
    if (!m_readings)
        return 0;
    int n = 0;
    for (Flashcard &c : m_cards) {
        if (!c.reading.isEmpty() || c.answer.isEmpty())
            continue;
        const QString r = m_readings->reading(c.answer);
        if (!r.isEmpty()) {
            c.reading = r;
            ++n;
        }
    }
    if (n > 0)
        emit changed();
    return n;
}

QString FlashcardModel::answerWithReading(const Flashcard &card)
{
    return ReadingStore::displayWithReading(card.answer, card.reading);
}

void FlashcardModel::addCard(const Flashcard &card)
{
    beginInsertRows(QModelIndex(), m_cards.size(), m_cards.size());
    m_cards.append(card);
    endInsertRows();
    emit changed();
}

void FlashcardModel::addClozeCardsFromSentence(const Sentence &s, const QString &mediaTitle, int count)
{
    if (!s.valid())
        return;
    // 语言感知分词：日语按分词器切词（私は学生です → 私/は/学生/です）
    const QStringList words = Tokenizer::tokenize(s.text, s.lang);
    if (words.size() < 3)
        return;
    const QString sep = s.japanese() ? QString() : QStringLiteral(" ");

    // 挖出最重要的词（日语词普遍较短：≥2；英语沿用 ≥4 优先）
    const int preferLen = s.japanese() ? 2 : 4;
    QVector<int> idx;
    for (int i = 0; i < words.size(); ++i) {
        if (words[i].size() >= preferLen)
            idx << i;
    }
    if (idx.size() < count) {
        idx.clear();
        for (int i = 0; i < words.size(); ++i)
            idx << i;
    }
    int step = qMax(1, idx.size() / qMax(1, count));
    int used = 0;
    for (int k = 0; k < idx.size() && used < count; k += step) {
        const int wi = idx[k];
        QStringList parts;
        for (int i = 0; i < words.size(); ++i)
            parts << (i == wi ? QStringLiteral("____") : words[i]);
        Flashcard c;
        c.prompt = parts.join(sep);
        c.answer = words[wi];
        // M12：答案词的假名读音（查词典已写入读音缓存），无缓存时留空
        if (m_readings)
            c.reading = m_readings->reading(words[wi]);
        c.source = s.text;
        c.media = mediaTitle;
        c.sentenceId = s.id;
        c.due = QDateTime::currentMSecsSinceEpoch();
        c.interval = 0.0;
        c.ease = 2.5;
        addCard(c);
        ++used;
    }
}

void FlashcardModel::removeCard(int row)
{
    if (row < 0 || row >= m_cards.size())
        return;
    beginRemoveRows(QModelIndex(), row, row);
    m_cards.removeAt(row);
    endRemoveRows();
    emit changed();
}

void FlashcardModel::clear()
{
    if (m_cards.isEmpty())
        return;
    beginResetModel();
    m_cards.clear();
    endResetModel();
    emit changed();
}

void FlashcardModel::applySm2(Flashcard &card, int quality)
{
    quality = qBound(0, quality, 5);
    if (quality < 3) {
        card.interval = 0.0;
        card.lapses++;
        card.ease = qMax(1.3, card.ease - 0.2);
    } else {
        if (card.interval == 0.0)
            card.interval = 1.0;
        else if (card.interval == 1.0)
            card.interval = 6.0;
        else
            card.interval *= card.ease;
        card.ease += (0.1 - (5 - quality) * (0.08 + (5 - quality) * 0.02));
        card.ease = qMax(1.3, card.ease);
    }
    const int days = int(card.interval);
    card.due = QDateTime::currentMSecsSinceEpoch() + qint64(days) * 86400000;
    if (quality >= 3)
        card.due += qint64(10) * 60000; // 当日再练 10 分钟（新卡）
}

void FlashcardModel::rate(int row, int quality)
{
    if (row < 0 || row >= m_cards.size())
        return;
    applySm2(m_cards[row], quality);
    dataChanged(index(row), index(row));
    emit changed();
}

QVector<int> FlashcardModel::dueRows(qint64 nowMs) const
{
    QVector<int> rows;
    for (int i = 0; i < m_cards.size(); ++i) {
        if (m_cards[i].due <= nowMs)
            rows << i;
    }
    return rows;
}

bool FlashcardModel::loadFrom(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    f.close();
    if (perr.error != QJsonParseError::NoError || !doc.isArray())
        return false;

    beginResetModel();
    m_cards.clear();
    for (const QJsonValue &v : doc.array()) {
        const QJsonObject o = v.toObject();
        Flashcard c;
        c.prompt = o.value(QStringLiteral("prompt")).toString();
        c.answer = o.value(QStringLiteral("answer")).toString();
        c.reading = o.value(QStringLiteral("reading")).toString(); // M12：旧数据缺字段 → 空
        c.source = o.value(QStringLiteral("source")).toString();
        c.media = o.value(QStringLiteral("media")).toString();
        c.sentenceId = o.value(QStringLiteral("sentenceId")).toInt(-1);
        c.due = qint64(o.value(QStringLiteral("due")).toDouble());
        c.interval = o.value(QStringLiteral("interval")).toDouble();
        c.ease = o.value(QStringLiteral("ease")).toDouble(2.5);
        c.lapses = o.value(QStringLiteral("lapses")).toInt();
        if (!c.prompt.isEmpty() && !c.answer.isEmpty())
            m_cards.append(c);
    }
    endResetModel();
    fillMissingReadings(); // M12：旧卡缺读音时用读音缓存补齐（缓存为空则不动）
    emit changed();
    return true;
}

bool FlashcardModel::saveTo(const QString &path) const
{
    QJsonArray arr;
    for (const Flashcard &c : m_cards) {
        QJsonObject o;
        o.insert(QStringLiteral("prompt"), c.prompt);
        o.insert(QStringLiteral("answer"), c.answer);
        o.insert(QStringLiteral("reading"), c.reading);
        o.insert(QStringLiteral("source"), c.source);
        o.insert(QStringLiteral("media"), c.media);
        o.insert(QStringLiteral("sentenceId"), c.sentenceId);
        o.insert(QStringLiteral("due"), double(c.due));
        o.insert(QStringLiteral("interval"), c.interval);
        o.insert(QStringLiteral("ease"), c.ease);
        o.insert(QStringLiteral("lapses"), c.lapses);
        arr.append(o);
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    f.close();
    return true;
}

} // namespace adoloop
