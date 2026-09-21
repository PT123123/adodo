#pragma once

#include "MediaEngine.h"

#include <QAudioOutput>
#include <QMediaPlayer>

namespace adoloop {

// 回退播放后端：基于 Qt Multimedia。变速功能取决于系统音频后端，
// 部分平台变速会变调（文档注明，推荐 mpv 后端）。
class QtMediaEngine : public MediaEngine {
    Q_OBJECT
public:
    explicit QtMediaEngine(QObject *parent = nullptr);

    Backend backend() const override { return Backend::Qt; }
    QString backendName() const override { return QStringLiteral("Qt Multimedia"); }

    bool open(const QString &source) override;
    void play() override;
    void pause() override;
    void stop() override;
    bool seek(Ms pos) override;
    void setRate(double rate) override;
    double rate() const override { return m_rate; }
    void setVolume(int volume) override;
    int volume() const override;
    Ms position() const override { return m_player.position(); }
    Ms duration() const override { return m_player.duration(); }
    bool isPlaying() const override;
    bool isLoaded() const override;
    QString lastError() const override { return m_lastError; }

private:
    QMediaPlayer m_player;
    QAudioOutput m_audioOutput;
    QString m_lastError;
    double m_rate = 1.0;
};

} // namespace adoloop
