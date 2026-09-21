#include "TranscriptPanel.h"
#include "../core/SubtitleModel.h"

#include <QAction>
#include <QMenu>
#include <QMouseEvent>
#include <QVBoxLayout>

namespace adoloop {

TranscriptListModel::TranscriptListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void TranscriptListModel::setSubtitleModel(SubtitleModel *model)
{
    beginResetModel();
    if (m_model)
        m_model->disconnect(this);
    m_model = model;
    if (m_model) {
        connect(m_model, &SubtitleModel::changed, this, [this]() {
            beginResetModel();
            endResetModel();
        });
    }
    endResetModel();
}

int TranscriptListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() || !m_model ? 0 : m_model->count();
}

QVariant TranscriptListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || !m_model || index.row() >= m_model->count())
        return {};
    const Sentence &s = m_model->at(index.row());
    switch (role) {
    case TextRole:
        return s.text;
    case TranslationRole:
        return s.translation;
    case TimeRole:
        return s.start;
    case SentenceIndexRole:
        return index.row();
    default:
        return {};
    }
}

QHash<int, QByteArray> TranscriptListModel::roleNames() const
{
    return {
        {TextRole, "text"},
        {TranslationRole, "translation"},
        {TimeRole, "time"},
        {SentenceIndexRole, "sentenceIndex"},
    };
}

TranscriptPanel::TranscriptPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    m_model = new TranscriptListModel(this);
    m_list = new QListView(this);
    m_list->setModel(m_model);
    m_list->setUniformItemSizes(true);
    m_list->setWordWrap(true);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);

    lay->addWidget(m_list);

    connect(m_list, &QListView::clicked, this, [this](const QModelIndex &idx) {
        if (idx.isValid())
            emit sentenceActivated(idx.row());
    });
    connect(m_list, &QWidget::customContextMenuRequested, this,
            &TranscriptPanel::onContextMenu);
}

void TranscriptPanel::setSubtitleModel(SubtitleModel *model)
{
    m_model->setSubtitleModel(model);
}

void TranscriptPanel::setCurrentSentence(int index)
{
    if (index == m_currentIndex)
        return;
    m_currentIndex = index;
    if (index >= 0 && index < m_model->rowCount()) {
        m_list->setCurrentIndex(m_model->index(index));
        m_list->scrollTo(m_model->index(index), QAbstractItemView::EnsureVisible);
    }
}

void TranscriptPanel::onContextMenu(const QPoint &pos)
{
    const QModelIndex idx = m_list->indexAt(pos);
    if (!idx.isValid())
        return;
    const int row = idx.row();

    QMenu menu(this);
    QAction *repeat = menu.addAction(QStringLiteral("从此句复读"));
    QAction *dict = menu.addAction(QStringLiteral("听写本句"));
    QAction *lookup = menu.addAction(QStringLiteral("查词（当前字幕词）"));
    QAction *book = menu.addAction(QStringLiteral("加入难重点句库"));
    QAction *vocab = menu.addAction(QStringLiteral("本句所有词加入生词本"));
    QAction *act = menu.exec(m_list->viewport()->mapToGlobal(pos));
    if (act == repeat)
        emit repeatRequested(row);
    else if (act == dict)
        emit dictationRequested(row);
    else if (act == lookup) {
        const Sentence &s = m_model->subtitleModel()->at(row);
        const QStringList words = s.wordList();
        if (!words.isEmpty())
            emit lookupWordRequested(words.first(), row);
    } else if (act == book)
        emit addToSentenceBook(row);
    else if (act == vocab)
        emit addAllWordsToVocabulary(row);
}

} // namespace adoloop
