#include "RetellWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTextEdit>
#include <QVBoxLayout>

namespace adoloop {

RetellWidget::RetellWidget(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    refresh();
}

void RetellWidget::buildUi()
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 4, 6, 4);
    lay->setSpacing(4);

    m_stateLabel = new QLabel(QStringLiteral("段落复述：听原音 → 隐藏原文回忆 → 复述 → 揭晓对照 → 自评"), this);
    m_stateLabel->setWordWrap(true);
    m_stateLabel->setStyleSheet(QStringLiteral("color:#ffd75e;"));
    lay->addWidget(m_stateLabel);

    // 范围 + 段落句数 + 录音开关
    m_scopeBox = new QComboBox(this);
    m_scopeBox->addItem(QStringLiteral("当前句"), int(RetellSession::Scope::Sentence));
    m_scopeBox->addItem(QStringLiteral("意群"), int(RetellSession::Scope::Chunk));
    m_scopeBox->addItem(QStringLiteral("段落（连续 N 句）"), int(RetellSession::Scope::Paragraph));
    m_scopeBox->addItem(QStringLiteral("全文"), int(RetellSession::Scope::FullScript));
    connect(m_scopeBox, &QComboBox::currentTextChanged, this, [this](const QString &) {
        onScopeChanged();
        emit optionsChanged();
    });
    {
        auto *row = new QHBoxLayout();
        row->addWidget(new QLabel(QStringLiteral("范围"), this));
        row->addWidget(m_scopeBox, 1);
        lay->addLayout(row);
    }

    m_paragraphSpin = new QSpinBox(this);
    m_paragraphSpin->setRange(1, 20);
    m_paragraphSpin->setValue(3);
    m_paragraphSpin->setSuffix(QStringLiteral(" 句"));
    connect(m_paragraphSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        emit optionsChanged();
    });
    {
        auto *row = new QHBoxLayout();
        row->addWidget(new QLabel(QStringLiteral("段落长度"), this));
        row->addWidget(m_paragraphSpin);
        m_recordBox = new QCheckBox(QStringLiteral("复述时录音"), this);
        m_recordBox->setChecked(true);
        m_recordBox->setToolTip(QStringLiteral("关闭或没有麦克风时：静默回忆 → 揭晓对照 → 自评"));
        connect(m_recordBox, &QCheckBox::toggled, this, [this](bool) { emit optionsChanged(); });
        row->addWidget(m_recordBox);
        row->addStretch(1);
        lay->addLayout(row);
    }

    m_posLabel = new QLabel(this);
    m_posLabel->setStyleSheet(QStringLiteral("color:#888;"));
    lay->addWidget(m_posLabel);

    m_text = new QTextEdit(this);
    m_text->setReadOnly(true);
    m_text->setMinimumHeight(90);
    m_text->setMaximumHeight(160);
    m_text->setStyleSheet(QStringLiteral("QTextEdit { background:#22242b; color:#eee; }"));
    lay->addWidget(m_text, 1);

    // 控制按钮
    m_startBtn = new QPushButton(QStringLiteral("开始复述"), this);
    m_stopBtn = new QPushButton(QStringLiteral("停止"), this);
    {
        auto *row = new QHBoxLayout();
        row->addWidget(m_startBtn);
        row->addWidget(m_stopBtn);
        row->addStretch(1);
        lay->addLayout(row);
    }

    m_retellBtn = new QPushButton(QStringLiteral("开始复述（录音）"), this);
    m_doneBtn = new QPushButton(QStringLiteral("说完了"), this);
    {
        auto *row = new QHBoxLayout();
        row->addWidget(m_retellBtn);
        row->addWidget(m_doneBtn);
        row->addStretch(1);
        lay->addLayout(row);
    }

    m_revealBtn = new QPushButton(QStringLiteral("揭晓对照"), this);
    m_nextBtn = new QPushButton(QStringLiteral("下一段"), this);
    {
        auto *row = new QHBoxLayout();
        row->addWidget(m_revealBtn);
        row->addWidget(m_nextBtn);
        row->addStretch(1);
        lay->addLayout(row);
    }

    m_rateBox = new QComboBox(this);
    m_rateBox->addItem(QStringLiteral("0 完全说不出"), 0);
    m_rateBox->addItem(QStringLiteral("1 大量遗漏"), 1);
    m_rateBox->addItem(QStringLiteral("2 部分说出"), 2);
    m_rateBox->addItem(QStringLiteral("3 基本说出"), 3);
    m_rateBox->addItem(QStringLiteral("4 较流畅"), 4);
    m_rateBox->addItem(QStringLiteral("5 完整流利"), 5);
    m_rateBtn = new QPushButton(QStringLiteral("记录自评"), this);
    {
        auto *row = new QHBoxLayout();
        row->addWidget(new QLabel(QStringLiteral("自评"), this));
        row->addWidget(m_rateBox, 1);
        row->addWidget(m_rateBtn);
        lay->addLayout(row);
    }

    m_hintLabel = new QLabel(this);
    m_hintLabel->setWordWrap(true);
    m_hintLabel->setStyleSheet(QStringLiteral("color:#9ad0ff;"));
    lay->addWidget(m_hintLabel);

    lay->addWidget(new QLabel(QStringLiteral("练习历史"), this));
    m_history = new QListWidget(this);
    m_history->setMaximumHeight(96);
    lay->addWidget(m_history);

    connect(m_startBtn, &QPushButton::clicked, this, &RetellWidget::startRequested);
    connect(m_stopBtn, &QPushButton::clicked, this, &RetellWidget::stopRequested);
    connect(m_retellBtn, &QPushButton::clicked, this, &RetellWidget::startRetellingRequested);
    connect(m_doneBtn, &QPushButton::clicked, this, &RetellWidget::stopRetellingRequested);
    connect(m_revealBtn, &QPushButton::clicked, this, &RetellWidget::revealRequested);
    connect(m_nextBtn, &QPushButton::clicked, this, &RetellWidget::nextRequested);
    connect(m_rateBtn, &QPushButton::clicked, this,
            [this]() { emit rateRequested(m_rateBox->currentData().toInt()); });

    onScopeChanged();
}

RetellSession::Scope RetellWidget::scope() const
{
    return RetellSession::Scope(m_scopeBox->currentData().toInt());
}

void RetellWidget::setScope(RetellSession::Scope scope)
{
    const int idx = m_scopeBox->findData(int(scope));
    if (idx >= 0 && idx != m_scopeBox->currentIndex()) {
        const QSignalBlocker b(m_scopeBox);
        m_scopeBox->setCurrentIndex(idx);
        onScopeChanged();
    }
}

int RetellWidget::paragraphSize() const
{
    return m_paragraphSpin->value();
}

void RetellWidget::setParagraphSize(int n)
{
    const QSignalBlocker b(m_paragraphSpin);
    m_paragraphSpin->setValue(qBound(1, n, 20));
}

bool RetellWidget::recordEnabled() const
{
    return m_recordBox->isChecked();
}

void RetellWidget::setRecordEnabled(bool on)
{
    const QSignalBlocker b(m_recordBox);
    m_recordBox->setChecked(on);
}

void RetellWidget::setHint(const QString &text)
{
    m_hintLabel->setText(text);
}

void RetellWidget::showHistory(const QStringList &items, const QStringList &tips)
{
    m_history->clear();
    for (int i = 0; i < items.size(); ++i) {
        auto *it = new QListWidgetItem(items[i], m_history);
        if (i < tips.size())
            it->setToolTip(tips[i]);
    }
}

void RetellWidget::attachSession(RetellSession *session)
{
    m_session = session;
    refresh();
}

void RetellWidget::onScopeChanged()
{
    m_paragraphSpin->setEnabled(scope() == RetellSession::Scope::Paragraph);
}

void RetellWidget::refresh()
{
    using State = RetellSession::State;
    const bool has = m_session != nullptr;
    const State st = has ? m_session->state() : State::Idle;
    const bool active = has && st != State::Idle && st != State::Finished;
    const RetellSession::Segment *seg = has ? m_session->currentSegment() : nullptr;

    if (has)
        m_stateLabel->setText(QStringLiteral("复述状态：%1")
                                  .arg(RetellSession::stateDisplayName(st)));
    else
        m_stateLabel->setText(QStringLiteral("段落复述：听原音 → 隐藏原文回忆 → 复述 → 揭晓对照 → 自评"));

    // 段落位置
    m_posLabel->setText(active && seg && has
                            ? QStringLiteral("第 %1 / %2 段 · %3%4")
                                  .arg(m_session->currentIndex() + 1)
                                  .arg(m_session->segmentCount())
                                  .arg(seg->label(),
                                       m_session->micAvailable() ? QString() : QStringLiteral(" · 无麦克风（静默模式）"))
                            : QString());

    // 文本区：隐藏中只给占位提示，揭晓后显示原文 + 译文
    if (!has || !seg) {
        m_text->setPlainText(QStringLiteral(
            "点「开始复述」后：\n"
            "1) 播放原音（字幕浮层隐藏原文，看不到答案）\n"
            "2) 默想/默读（有麦克风可点「开始复述（录音）」）\n"
            "3) 「揭晓对照」原文与译文，再做自评\n\n"
            "范围可选：当前句 / 意群 / 段落（连续 N 句）/ 全文。\n"
            "没有麦克风也能用：自动降级为「静默回忆 → 揭晓对照 → 自评」。"));
    } else if (m_session->concealed()) {
        m_text->setPlainText(QStringLiteral("◇ 原文已隐藏（%1）\n\n先听原音，再回忆复述；完成后点「揭晓对照」。")
                                 .arg(seg->label()));
    } else {
        QString t = QStringLiteral("【%1】\n%2").arg(seg->label(), seg->text);
        if (!seg->translation.isEmpty())
            t += QStringLiteral("\n\n%1").arg(seg->translation);
        m_text->setPlainText(t);
    }

    // 按钮可用性（无麦克风时「开始复述（录音）」不可用 → 只能揭晓，即降级路径）
    m_startBtn->setEnabled(!active);
    m_stopBtn->setEnabled(active);
    m_retellBtn->setEnabled(has && st == State::Concealed && m_session->canRecord());
    m_doneBtn->setEnabled(has && st == State::Retelling);
    m_revealBtn->setEnabled(has && (st == State::Concealed || st == State::Retelling));
    m_nextBtn->setEnabled(active);
    const bool canRate = has && (st == State::Revealed || st == State::SelfRating);
    m_rateBox->setEnabled(canRate);
    m_rateBtn->setEnabled(canRate);

    // 使能含义与状态一致后，把提示行也复位成「当前状态该做什么」
    if (has && st == State::Concealed && !m_session->canRecord()) {
        m_hintLabel->setText(m_session->micAvailable()
                                 ? QStringLiteral("已关闭录音：静默回忆后点「揭晓对照」")
                                 : QStringLiteral("未检测到麦克风：静默回忆 → 揭晓对照 → 自评"));
    }
}

} // namespace adoloop
