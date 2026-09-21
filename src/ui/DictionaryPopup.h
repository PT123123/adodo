#pragma once

#include "../core/Types.h"

#include <QWidget>

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace adoloop {

// 查词弹窗：无边框置顶小窗，展示释义；支持加入生词本/发音。
class DictionaryPopup : public QWidget {
    Q_OBJECT
public:
    explicit DictionaryPopup(QWidget *parent = nullptr);

    void showEntry(const adoloop::DictEntry &entry, const QPoint &globalPos);
    void showLoading(const QString &word, const QPoint &globalPos);
    void showError(const QString &word, const QString &error, const QPoint &globalPos);
    void closePopup();

signals:
    void addToVocabularyRequested(const QString &word);
    // M11：带上音频 URL 供 core/PronunciationPlayer 播放（空 = 词典未提供发音）
    void pronounceRequested(const QString &word, const QString &audioUrl);
    void lookupRetry(const QString &word);

protected:
    void mousePressEvent(QMouseEvent *event) override;

private:
    void rebuild(const QString &title, const QString &body, const QPoint &globalPos);
    // 标题排版：日语「表记 [假名] 声调」；英语沿用「/美/ /英/」
    static QString buildTitle(const adoloop::DictEntry &entry);

    QVBoxLayout *m_lay = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_body = nullptr;
    QPushButton *m_addBtn = nullptr;
    QPushButton *m_pronounceBtn = nullptr;
    QPushButton *m_retryBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;
    QString m_word;
    QString m_audioUrl;
};

} // namespace adoloop
