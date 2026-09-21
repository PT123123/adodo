#pragma once

#include "../core/Types.h"

#include <QWidget>

class QPushButton;
class QSlider;
class QLabel;
class QComboBox;

namespace adoloop {

// 播放控制条：播放/暂停、停止、进度条、时间、变速、音量、A-B 区间。
class PlayerControls : public QWidget {
    Q_OBJECT
public:
    explicit PlayerControls(QWidget *parent = nullptr);

    void setPosition(Ms pos);
    void setDuration(Ms dur);
    void setPlaying(bool playing);
    void setLoaded(bool loaded);
    void setRate(double rate);
    void setVolume(int vol);
    void setAbMarkers(Ms a, Ms b); // -1 表示未设置

signals:
    void playPauseClicked();
    void stopClicked();
    void seekRequested(adoloop::Ms pos);
    void rateChanged(double rate);
    void volumeChanged(int volume);
    void abSetA();
    void abSetB();
    void abLoopToggled(bool on);

private:
    void updateTimeLabel();

    QPushButton *m_playBtn = nullptr;
    QPushButton *m_stopBtn = nullptr;
    QSlider *m_seekSlider = nullptr;
    QLabel *m_timeLabel = nullptr;
    QComboBox *m_rateCombo = nullptr;
    QSlider *m_volumeSlider = nullptr;
    QPushButton *m_abA = nullptr;
    QPushButton *m_abB = nullptr;
    QPushButton *m_abLoop = nullptr;

    Ms m_pos = 0;
    Ms m_dur = 0;
    bool m_playing = false;
    bool m_loaded = false;
};

} // namespace adoloop
