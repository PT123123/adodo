#pragma once

#include "../study/SentenceBuilder.h"

#include <QWidget>

class QLabel;
class QPushButton;
class QFlowLayout;

namespace adoloop {

// 造句练习界面：待选词 chips + 答案区 chips，点选重组。
class SentenceBuilderWidget : public QWidget {
    Q_OBJECT
public:
    explicit SentenceBuilderWidget(QWidget *parent = nullptr);

    void loadSentence(const QString &sentence);
    void refresh();
    void clear();

    // 学习语言（M11）：日语按分词器切 chip，英语与 M10 前一致
    void setLang(Lang lang) { m_builder.setLang(lang); }

signals:
    void pickRequested(int sourceIndex);
    void unpickRequested(int answerIndex);
    void checkRequested();
    void nextSentenceRequested();

private:
    SentenceBuilder m_builder;
    QLabel *m_prompt = nullptr;
    QWidget *m_answerBox = nullptr;
    QWidget *m_sourceBox = nullptr;
    QLabel *m_result = nullptr;
    QPushButton *m_checkBtn = nullptr;
    QPushButton *m_resetBtn = nullptr;
    QPushButton *m_nextBtn = nullptr;
};

} // namespace adoloop
