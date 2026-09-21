#include "DictationWidget.h"
#include "../study/DictationSession.h"

#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace adoloop {

DictationWidget::DictationWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 4, 6, 4);

    m_feedback = new QLabel(QStringLiteral("听写：播放句子后输入（抠词输单词，单句输整句回车）"), this);
    m_feedback->setWordWrap(true);

    m_slotBox = new QWidget(this);
    m_slotLay = new QGridLayout(m_slotBox);
    m_slotLay->setContentsMargins(0, 0, 0, 0);
    m_slotLay->setSpacing(4);

    auto *scroll = new QScrollArea(this);
    scroll->setWidget(m_slotBox);
    scroll->setWidgetResizable(true);
    scroll->setMaximumHeight(120);

    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(QStringLiteral("输入…（Enter 提交）"));
    m_input->setEnabled(false);

    m_beginBtn = new QPushButton(QStringLiteral("开始听写"), this);
    m_revealBtn = new QPushButton(QStringLiteral("显示答案"), this);
    m_nextBtn = new QPushButton(QStringLiteral("下一句"), this);
    m_stopBtn = new QPushButton(QStringLiteral("停止"), this);
    m_revealBtn->setEnabled(false);
    m_nextBtn->setEnabled(false);
    m_stopBtn->setEnabled(false);

    auto *row = new QHBoxLayout();
    row->addWidget(m_beginBtn);
    row->addWidget(m_revealBtn);
    row->addWidget(m_nextBtn);
    row->addWidget(m_stopBtn);
    row->addStretch(1);

    lay->addWidget(m_feedback);
    lay->addWidget(scroll, 1);
    lay->addWidget(m_input);
    lay->addLayout(row);

    connect(m_input, &QLineEdit::returnPressed, this, [this]() {
        emit inputSubmitted(m_input->text());
        if (!m_input->text().contains(QLatin1Char(' '))) {
            // 抠词模式：清空等待下一个词；单句模式保留直到回车判定
            const bool wordMode = m_session && m_session->mode() == DictationMode::Word;
            if (wordMode)
                m_input->clear();
        }
    });
    connect(m_beginBtn, &QPushButton::clicked, this,
            [this]() { emit beginRequested(m_session); });
    connect(m_revealBtn, &QPushButton::clicked, this, &DictationWidget::revealRequested);
    connect(m_nextBtn, &QPushButton::clicked, this, &DictationWidget::nextRequested);
    connect(m_stopBtn, &QPushButton::clicked, this, &DictationWidget::stopRequested);
}

void DictationWidget::attachSession(DictationSession *session)
{
    m_session = session;
    m_beginBtn->setEnabled(session != nullptr);
    m_revealBtn->setEnabled(session && session->state() != DictationSession::State::Idle);
    m_nextBtn->setEnabled(session && session->state() != DictationSession::State::Idle);
    m_stopBtn->setEnabled(session && session->state() != DictationSession::State::Idle);
    m_input->setEnabled(session && session->state() != DictationSession::State::Idle);
}

void DictationWidget::detachSession()
{
    m_session = nullptr;
    m_words.clear();
    m_filled.clear();
    rebuildSlots();
    m_input->setEnabled(false);
    m_beginBtn->setEnabled(false);
    m_revealBtn->setEnabled(false);
    m_nextBtn->setEnabled(false);
    m_stopBtn->setEnabled(false);
}

void DictationWidget::refreshSlots(const QStringList &words, const QVector<bool> &filled)
{
    m_words = words;
    m_filled = filled;
    rebuildSlots();
}

void DictationWidget::setFeedback(bool ok)
{
    m_feedback->setText(ok ? QStringLiteral("✓ 正确")
                           : QStringLiteral("✗ 有误，请再试（或显示答案）"));
    m_feedback->setStyleSheet(ok ? QStringLiteral("color:#2ecc71;")
                                 : QStringLiteral("color:#e74c3c;"));
}

void DictationWidget::showReport(const QString &summary)
{
    m_feedback->setText(QStringLiteral("听写完成：%1").arg(summary));
    m_feedback->setStyleSheet(QStringLiteral("color:#ffd75e;"));
}

void DictationWidget::rebuildSlots()
{
    // 清空
    while (QLayoutItem *item = m_slotLay->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    const int cols = qMax(1, m_words.size() > 8 ? 6 : m_words.size());
    for (int i = 0; i < m_words.size(); ++i) {
        const bool ok = i < m_filled.size() && m_filled[i];
        auto *label = new QLabel(ok ? m_words[i] : QStringLiteral("______"), m_slotBox);
        label->setAlignment(Qt::AlignCenter);
        label->setStyleSheet(ok ? QStringLiteral("background:#1e3a2a;color:#7ddfa0;border-radius:4px;padding:4px 8px;")
                                : QStringLiteral("background:#3a3d4a;color:#aaa;border-radius:4px;padding:4px 8px;"));
        m_slotLay->addWidget(label, i / cols, i % cols);
    }
    m_slotBox->adjustSize();
}

} // namespace adoloop
