#pragma once

#include "../core/Types.h"

#include <QObject>
#include <QString>

namespace adoloop {

// 播放引擎抽象。实现：MpvMediaEngine（首选，变速不变调）、QtMediaEngine（回退）。
class MediaEngine : public QObject {
    Q_OBJECT
public:
    enum class Backend { None, Mpv, Qt };
    Q_ENUM(Backend)

    explicit MediaEngine(QObject *parent = nullptr) : QObject(parent) {}
    ~MediaEngine() override = default;

    virtual Backend backend() const = 0;
    virtual QString backendName() const = 0;

    // 打开本地文件或 URL（mpv 后端由 AppController 先经 yt-dlp 解析）
    virtual bool open(const QString &source) = 0;

    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    virtual bool seek(Ms pos) = 0;

    // 变速不变调（mpv 后端：speed + pitch correction）
    virtual void setRate(double rate) = 0;
    virtual double rate() const = 0;
    virtual void setVolume(int volume) = 0; // 0..100
    virtual int volume() const = 0;

    virtual Ms position() const = 0;
    virtual Ms duration() const = 0;
    virtual bool isPlaying() const = 0;
    virtual bool isLoaded() const = 0;
    virtual QString lastError() const = 0;

    // 创建后端：优先 mpv，不可用时回退 Qt
    static MediaEngine *create(QObject *parent);

signals:
    void positionChanged(adoloop::Ms pos);
    void durationChanged(adoloop::Ms dur);
    void playStateChanged(bool playing);
    void eofReached();
    void loadedChanged(bool loaded);
    void errorOccurred(const QString &message);
};

} // namespace adoloop
