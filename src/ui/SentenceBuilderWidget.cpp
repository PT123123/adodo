#include "SentenceBuilderWidget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace adoloop {

SentenceBuilderWidget::SentenceBuilderWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 4, 6, 4);

    m_prompt = new QLabel(QStringLiteral("造句练习：点击单词组成句子"), this);
    m_prompt->setWordWrap(true);

    m_answerBox = new QWidget(this);
    m_answerBox->setStyleSheet(QStringLiteral("background:#2b2d35;border-radius:6px;"));

    auto *scrollSrc = new QScrollArea(this);
    m_sourceBox = new QWidget(this);
    auto *srcLay = new QVBoxLayout(m_sourceBox);
    srcLay->setContentsMargins(4, 4, 4, 4);
    srcLay->setSpacing(4);
    scrollSrc->setWidget(m_sourceBox);
    scrollSrc->setWidgetResizable(true);
    scrollSrc->setMinimumHeight(60);

    m_result = new QLabel(this);
    m_result->setWordWrap(true);

    m_checkBtn = new QPushButton(QStringLiteral("检查"), this);
    m_resetBtn = new QPushButton(QStringLiteral("重排"), this);
    m_nextBtn = new QPushButton(QStringLiteral("下一句"), this);

    auto *row = new QHBoxLayout();
    row->addWidget(m_checkBtn);
    row->addWidget(m_resetBtn);
    row->addWidget(m_nextBtn);
    row->addStretch(1);

    lay->addWidget(m_prompt);
    lay->addWidget(m_answerBox, 1);
    lay->addWidget(scrollSrc, 1);
    lay->addWidget(m_result);
    lay->addLayout(row);

    connect(m_checkBtn, &QPushButton::clicked, this, [this]() {
        if (m_builder.check()) {
            m_result->setText(QStringLiteral("✓ 正确！"));
            m_result->setStyleSheet(QStringLiteral("color:#2ecc71;"));
        } else {
            m_result->setText(QStringLiteral("✗ 不正确，请再试"));
            m_result->setStyleSheet(QStringLiteral("color:#e74c3c;"));
        }
    });
    connect(m_resetBtn, &QPushButton::clicked, this, [this]() {
        m_builder.reset();
        refresh();
        m_result->clear();
    });
    connect(m_nextBtn, &QPushButton::clicked, this, &SentenceBuilderWidget::nextSentenceRequested);
}

void SentenceBuilderWidget::loadSentence(const QString &sentence)
{
    m_builder.load(sentence);
    m_prompt->setText(sentence);
    refresh();
    m_result->clear();
}

void SentenceBuilderWidget::clear()
{
    m_prompt->setText(QStringLiteral("造句练习"));
    m_result->clear();
    m_builder = SentenceBuilder{};
    refresh();
}

void SentenceBuilderWidget::refresh()
{
    // 答案区：已选 chips
    while (QLayoutItem *item = m_answerBox->layout() ? m_answerBox->layout()->takeAt(0) : nullptr) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    delete m_answerBox->layout();

    auto *ansLay = new QHBoxLayout(m_answerBox);
    ansLay->setContentsMargins(6, 6, 6, 6);
    ansLay->setSpacing(6);
    const QStringList ans = m_builder.answerChips();
    for (int i = 0; i < ans.size(); ++i) {
        auto *btn = new QPushButton(ans[i], m_answerBox);
        btn->setStyleSheet(QStringLiteral("background:#2e6da4;color:white;border-radius:4px;padding:4px 10px;"));
        connect(btn, &QPushButton::clicked, this, [this, i]() {
            m_builder.unpick(i);
            emit unpickRequested(i);
            refresh();
        });
        ansLay->addWidget(btn);
    }
    ansLay->addStretch(1);

    // 待选区
    while (QLayoutItem *item = m_sourceBox->layout()->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    auto *srcLay = new QHBoxLayout(m_sourceBox);
    srcLay->setContentsMargins(4, 4, 4, 4);
    srcLay->setSpacing(6);
    const QStringList src = m_builder.sourceChips();
    for (int i = 0; i < src.size(); ++i) {
        auto *btn = new QPushButton(src[i], m_sourceBox);
        btn->setStyleSheet(QStringLiteral("background:#4a4e5e;color:#eee;border-radius:4px;padding:4px 10px;"));
        connect(btn, &QPushButton::clicked, this, [this, i]() {
            m_builder.pick(i);
            emit pickRequested(i);
            refresh();
        });
        srcLay->addWidget(btn);
    }
    srcLay->addStretch(1);
}

} // namespace adoloop
