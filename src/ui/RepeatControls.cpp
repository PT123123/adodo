#include "RepeatControls.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>

namespace adoloop {

RepeatControls::RepeatControls(QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(4, 2, 4, 2);
    lay->setSpacing(4);

    m_prev = new QPushButton(QStringLiteral("上一句 (P)"), this);
    m_next = new QPushButton(QStringLiteral("下一句 (N)"), this);
    m_repeat = new QPushButton(QStringLiteral("本句复读 (R)"), this);
    m_fullScript = new QPushButton(QStringLiteral("全文复读"), this);
    m_fullScript->setCheckable(true);

    lay->addWidget(new QLabel(QStringLiteral("重复:"), this));
    m_countSpin = new QSpinBox(this);
    m_countSpin->setRange(1, 20);
    m_countSpin->setValue(1);
    lay->addWidget(m_countSpin);

    lay->addWidget(new QLabel(QStringLiteral("间隔ms:"), this));
    m_gapSpin = new QSpinBox(this);
    m_gapSpin->setRange(0, 5000);
    m_gapSpin->setSingleStep(100);
    m_gapSpin->setValue(500);
    lay->addWidget(m_gapSpin);

    lay->addStretch(1);
    lay->addWidget(m_prev);
    lay->addWidget(m_next);
    lay->addWidget(m_repeat);
    lay->addWidget(m_fullScript);

    connect(m_prev, &QPushButton::clicked, this, &RepeatControls::prevSentenceRequested);
    connect(m_next, &QPushButton::clicked, this, &RepeatControls::nextSentenceRequested);
    connect(m_repeat, &QPushButton::clicked, this, &RepeatControls::repeatSentenceRequested);
    connect(m_fullScript, &QPushButton::toggled, this, &RepeatControls::fullScriptToggled);
    connect(m_countSpin, &QSpinBox::valueChanged, this, [this](int v) {
        emit repeatConfigChanged(v, m_gapSpin->value());
    });
    connect(m_gapSpin, &QSpinBox::valueChanged, this, [this](int v) {
        emit repeatConfigChanged(m_countSpin->value(), v);
    });
}

int RepeatControls::repeatCount() const { return m_countSpin->value(); }
int RepeatControls::gapMs() const { return m_gapSpin->value(); }
bool RepeatControls::fullScriptMode() const { return m_fullScript->isChecked(); }

} // namespace adoloop
