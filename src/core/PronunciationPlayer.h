#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

class QAudioOutput;
class QMediaPlayer;

namespace adoloop {

// 词典发音播放（M11）：独立 QMediaPlayer + QAudioOutput。
//
// 关键约束：**绝不打断 mpv 主播放**——本类不碰 MediaEngine，也不使用它的
// 播放器实例；mpv 以独立进程播放，Qt 只播一小段词典音频，两者互不干扰。
// 词典音频可来自网络（有道 dictvoice，https + audio/mpeg）或本地文件。
class PronunciationPlayer : public QObject {
    Q_OBJECT
public:
    explicit PronunciationPlayer(QObject *parent = nullptr);
    ~PronunciationPlayer() override;

    // 播放给定 URL（http/https/file 或本地路径）；空/非法 URL → failed()
    void play(const QString &urlOrPath);
    void stop();
    bool isPlaying() const;

    // 0..100（与播放器音量设置一致）
    void setVolume(int volume);
    int volume() const { return m_volume; }

    // 是否是可播放的音频地址
    static bool isPlayable(const QString &urlOrPath);

signals:
    void started(const QString &url);
    void finished();
    void failed(const QString &error);

private:
    QString normalizeSource(const QString &urlOrPath) const;

    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audio = nullptr;
    int m_volume = 90;
    QString m_current;
    bool m_errorPending = false;
};

} // namespace adoloop
