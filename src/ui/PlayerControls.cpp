#include "PlayerControls.h"
#include "../util/TimeUtil.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QStyle>

namespace adoloop {

PlayerControls::PlayerControls(QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(4, 2, 4, 2);
    lay->setSpacing(4);

    m_playBtn = new QPushButton(this);
    m_playBtn->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_playBtn->setToolTip(QStringLiteral("播放/暂停 (Space)"));
    m_stopBtn = new QPushButton(this);
    m_stopBtn->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    m_stopBtn->setToolTip(QStringLiteral("停止 (S)"));

    m_seekSlider = new QSlider(Qt::Horizontal, this);
    m_seekSlider->setMinimum(0);
    m_seekSlider->setMaximum(0);
    m_seekSlider->setEnabled(false);

    m_timeLabel = new QLabel(QStringLiteral("00:00:00 / 00:00:00"), this);

    m_rateCombo = new QComboBox(this);
    for (const QString &r : {QStringLiteral("0.25x"), QStringLiteral("0.5x"),
                             QStringLiteral("0.75x"), QStringLiteral("1.0x"),
                             QStringLiteral("1.25x"), QStringLiteral("1.5x"),
                             QStringLiteral("2.0x")}) {
        m_rateCombo->addItem(r);
    }
    m_rateCombo->setCurrentIndex(3);
    m_rateCombo->setToolTip(QStringLiteral("变速不变调"));

    m_volumeSlider = new QSlider(Qt::Horizontal, this);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(80);
    m_volumeSlider->setFixedWidth(80);
    m_volumeSlider->setToolTip(QStringLiteral("音量"));

    m_abA = new QPushButton(QStringLiteral("A"), this);
    m_abB = new QPushButton(QStringLiteral("B"), this);
    m_abLoop = new QPushButton(QStringLiteral("A-B 循环"), this);
    m_abLoop->setCheckable(true);
    m_abA->setToolTip(QStringLiteral("标记区间起点 A"));
    m_abB->setToolTip(QStringLiteral("标记区间终点 B"));

    lay->addWidget(m_playBtn);
    lay->addWidget(m_stopBtn);
    lay->addWidget(m_seekSlider, 1);
    lay->addWidget(m_timeLabel);
    lay->addWidget(m_rateCombo);
    lay->addWidget(m_volumeSlider);
    lay->addWidget(m_abA);
    lay->addWidget(m_abB);
    lay->addWidget(m_abLoop);

    connect(m_playBtn, &QPushButton::clicked, this, &PlayerControls::playPauseClicked);
    connect(m_stopBtn, &QPushButton::clicked, this, &PlayerControls::stopClicked);
    connect(m_seekSlider, &QSlider::sliderMoved, this, [this](int v) {
        emit seekRequested(Ms(v));
    });
    connect(m_rateCombo, &QComboBox::currentTextChanged, this, [this](const QString &t) {
        QString r = t;
        r.chop(1); // 去掉 'x'
        emit rateChanged(r.toDouble());
    });
    connect(m_volumeSlider, &QSlider::valueChanged, this, &PlayerControls::volumeChanged);
    connect(m_abA, &QPushButton::clicked, this, &PlayerControls::abSetA);
    connect(m_abB, &QPushButton::clicked, this, &PlayerControls::abSetB);
    connect(m_abLoop, &QPushButton::toggled, this, &PlayerControls::abLoopToggled);
}

void PlayerControls::setPosition(Ms pos)
{
    m_pos = pos;
    updateTimeLabel();
    if (m_seekSlider->maximum() > 0 && !m_seekSlider->isSliderDown())
        m_seekSlider->setValue(int(pos));
}

void PlayerControls::setDuration(Ms dur)
{
    m_dur = dur;
    m_seekSlider->setMaximum(int(dur));
    m_seekSlider->setEnabled(dur > 0);
    updateTimeLabel();
}

void PlayerControls::setPlaying(bool playing)
{
    m_playing = playing;
    m_playBtn->setIcon(style()->standardIcon(playing ? QStyle::SP_MediaPause
                                                     : QStyle::SP_MediaPlay));
}

void PlayerControls::setLoaded(bool loaded)
{
    m_loaded = loaded;
    m_playBtn->setEnabled(loaded);
    m_stopBtn->setEnabled(loaded);
}

void PlayerControls::setRate(double rate)
{
    const QString target = QString::number(rate, 'g', 2) + QStringLiteral("x");
    const int idx = m_rateCombo->findText(target);
    if (idx >= 0)
        m_rateCombo->setCurrentIndex(idx);
}

void PlayerControls::setVolume(int vol)
{
    m_volumeSlider->setValue(qBound(0, vol, 100));
}

void PlayerControls::setAbMarkers(Ms a, Ms b)
{
    m_abA->setProperty("set", a >= 0);
    m_abB->setProperty("set", b >= 0);
    m_abA->setStyleSheet(a >= 0 ? QStringLiteral("color:#e74c3c;font-weight:bold;") : QString());
    m_abB->setStyleSheet(b >= 0 ? QStringLiteral("color:#2980b9;font-weight:bold;") : QString());
}

void PlayerControls::updateTimeLabel()
{
    m_timeLabel->setText(QStringLiteral("%1 / %2")
                             .arg(timeutil::toClock(m_pos), timeutil::toClock(m_dur)));
}

} // namespace adoloop
