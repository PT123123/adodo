#pragma once

#include "../core/Types.h"

#include <QAbstractListModel>
#include <QListView>
#include <QWidget>

namespace adoloop {

class SubtitleModel;

// 字幕侧栏模型：包装 SubtitleModel，供列表显示（原文 + 译文 + 时间）。
class TranscriptListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role {
        TextRole = Qt::UserRole + 1,
        TranslationRole,
        TimeRole,
        SentenceIndexRole,
    };

    explicit TranscriptListModel(QObject *parent = nullptr);

    void setSubtitleModel(SubtitleModel *model);
    SubtitleModel *subtitleModel() const { return m_model; }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    SubtitleModel *m_model = nullptr;
};

// 字幕侧栏：逐句列表，点击跳转；右键菜单（复读/听写/查词/加句库/加生词）。
class TranscriptPanel : public QWidget {
    Q_OBJECT
public:
    explicit TranscriptPanel(QWidget *parent = nullptr);

    void setSubtitleModel(SubtitleModel *model);
    void setCurrentSentence(int index);
    int currentSentence() const { return m_currentIndex; }

signals:
    void sentenceActivated(int index);
    void repeatRequested(int index);
    void dictationRequested(int index);
    void lookupWordRequested(const QString &word, int sentenceIndex);
    void addToSentenceBook(int index);
    void addAllWordsToVocabulary(int index);

private:
    void onContextMenu(const QPoint &pos);

    QListView *m_list = nullptr;
    TranscriptListModel *m_model = nullptr;
    int m_currentIndex = -1;
};

} // namespace adoloop
