#include "QtMediaEngine.h"
#include "../app/Settings.h"

namespace adoloop {

QtMediaEngine::QtMediaEngine(QObject *parent)
    : MediaEngine(parent)
{
    m_audioOutput.setVolume(qreal(Settings::instance().volume()) / 100.0);
    m_player.setAudioOutput(&m_audioOutput);

    connect(&m_player, &QMediaPlayer::positionChanged, this,
            &MediaEngine::positionChanged);
    connect(&m_player, &QMediaPlayer::durationChanged, this,
            &MediaEngine::durationChanged);
    connect(&m_player, &QMediaPlayer::playbackStateChanged, this,
            [this](QMediaPlayer::PlaybackState st) {
                emit playStateChanged(st == QMediaPlayer::PlayingState);
            });
    connect(&m_player, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus st) {
                if (st == QMediaPlayer::LoadedMedia)
                    emit loadedChanged(true);
                else if (st == QMediaPlayer::NoMedia)
                    emit loadedChanged(false);
                if (st == QMediaPlayer::EndOfMedia)
                    emit eofReached();
            });
    connect(&m_player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString &err) {
                m_lastError = err;
                emit errorOccurred(err);
            });
}

bool QtMediaEngine::open(const QString &source)
{
    m_lastError.clear();
    if (source.startsWith(QLatin1String("http://"))
        || source.startsWith(QLatin1String("https://"))) {
        m_player.setSource(QUrl(source));
    } else {
        m_player.setSource(QUrl::fromLocalFile(source));
    }
    return true;
}

void QtMediaEngine::play() { m_player.play(); }
void QtMediaEngine::pause() { m_player.pause(); }
void QtMediaEngine::stop() { m_player.stop(); }

bool QtMediaEngine::seek(Ms pos)
{
    m_player.setPosition(qMax<qint64>(0, pos));
    return true;
}

void QtMediaEngine::setRate(double rate)
{
    m_rate = qBound(0.25, rate, 2.0);
    m_player.setPlaybackRate(m_rate);
}

void QtMediaEngine::setVolume(int volume)
{
    m_audioOutput.setVolume(qreal(qBound(0, volume, 100)) / 100.0);
}

int QtMediaEngine::volume() const
{
    return int(qRound(m_audioOutput.volume() * 100.0));
}

bool QtMediaEngine::isPlaying() const
{
    return m_player.playbackState() == QMediaPlayer::PlayingState;
}

bool QtMediaEngine::isLoaded() const
{
    const auto st = m_player.mediaStatus();
    return st == QMediaPlayer::LoadedMedia || st == QMediaPlayer::BufferedMedia
        || st == QMediaPlayer::BufferingMedia;
}

} // namespace adoloop
