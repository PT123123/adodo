#include "PronunciationPlayer.h"

#include <QAudioOutput>
#include <QDir>
#include <QFileInfo>
#include <QMediaPlayer>
#include <QUrl>

namespace adoloop {

PronunciationPlayer::PronunciationPlayer(QObject *parent)
    : QObject(parent)
{
    m_player = new QMediaPlayer(this);
    m_audio = new QAudioOutput(this);
    m_player->setAudioOutput(m_audio);
    m_audio->setVolume(m_volume / 100.0f);

    connect(m_player, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus st) {
                if (st == QMediaPlayer::EndOfMedia || st == QMediaPlayer::InvalidMedia) {
                    if (st == QMediaPlayer::EndOfMedia)
                        emit finished();
                }
            });
    connect(m_player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString &err) {
                m_errorPending = true;
                emit failed(err.isEmpty() ? QStringLiteral("音频播放失败（格式或网络问题）") : err);
            });
}

PronunciationPlayer::~PronunciationPlayer()
{
    if (m_player)
        m_player->stop();
}

bool PronunciationPlayer::isPlayable(const QString &urlOrPath)
{
    const QString s = urlOrPath.trimmed();
    if (s.isEmpty())
        return false;
    const QUrl u(s);
    if (u.isValid() && !u.scheme().isEmpty()) {
        const QString scheme = u.scheme().toLower();
        return scheme == QLatin1String("http") || scheme == QLatin1String("https")
            || scheme == QLatin1String("file") || scheme == QLatin1String("qrc");
    }
    return QFileInfo::exists(s); // 本地路径
}

QString PronunciationPlayer::normalizeSource(const QString &urlOrPath) const
{
    const QString s = urlOrPath.trimmed();
    const QUrl u(s);
    if (u.isValid() && !u.scheme().isEmpty())
        return s;
    return QUrl::fromLocalFile(QFileInfo(s).absoluteFilePath()).toString();
}

void PronunciationPlayer::play(const QString &urlOrPath)
{
    if (!isPlayable(urlOrPath)) {
        emit failed(QStringLiteral("没有可用的发音音频（词典未返回或地址无效）"));
        return;
    }
    const QString src = normalizeSource(urlOrPath);
    m_errorPending = false;
    m_current = src;
    m_player->stop();
    m_player->setSource(QUrl(src));
    m_player->setPosition(0);
    m_player->play();
    emit started(src);
}

void PronunciationPlayer::stop()
{
    if (m_player) {
        m_player->stop();
        m_player->setSource(QUrl());
    }
    m_current.clear();
}

bool PronunciationPlayer::isPlaying() const
{
    return m_player && m_player->playbackState() == QMediaPlayer::PlayingState;
}

void PronunciationPlayer::setVolume(int volume)
{
    m_volume = qBound(0, volume, 100);
    if (m_audio)
        m_audio->setVolume(m_volume / 100.0f);
}

} // namespace adoloop
