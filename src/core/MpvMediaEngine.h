#pragma once

#include "MediaEngine.h"

#include <QLocalSocket>
#include <QProcess>
#include <QTimer>

namespace adoloop {

// mpv 播放后端：QProcess 启动 mpv.exe，通过命名管道 JSON IPC 收发命令与事件。
// - 优点：变速不变调（--audio-pitch-correction=yes）、视频窗口由 mpv 渲染、字幕随播。
// - Windows 命名管道：\\.\pipe\adoloop-mpv-<pid>
// - 不编译期链接 libmpv，仅依赖 mpv.exe 存在于 PATH 或设置中指定路径。
class MpvMediaEngine : public MediaEngine {
    Q_OBJECT
public:
    explicit MpvMediaEngine(QObject *parent = nullptr);
    ~MpvMediaEngine() override;

    Backend backend() const override { return Backend::Mpv; }
    QString backendName() const override { return QStringLiteral("mpv (JSON IPC)"); }

    bool open(const QString &source) override;
    void play() override;
    void pause() override;
    void stop() override;
    bool seek(Ms pos) override;
    void setRate(double rate) override;
    double rate() const override { return m_rate; }
    void setVolume(int volume) override;
    int volume() const override { return m_volume; }
    Ms position() const override { return m_position; }
    Ms duration() const override { return m_duration; }
    bool isPlaying() const override { return m_playing; }
    bool isLoaded() const override { return m_loaded; }
    QString lastError() const override { return m_lastError; }

private:
    void startMpv();
    void sendCommand(const QVariantList &args);
    void sendSet(const QString &prop, const QVariant &value);
    void handleMessage(const QByteArray &line);
    void observeProperties();
    void onProcessExit();

    QProcess m_proc;
    QLocalSocket m_socket;
    QTimer m_reconnectTimer;
    QString m_pipeName;
    QString m_lastError;
    QString m_pendingSource;
    bool m_loaded = false;
    bool m_playing = false;
    Ms m_position = 0;
    Ms m_duration = 0;
    double m_rate = 1.0;
    int m_volume = 80;
    bool m_quitting = false;
};

} // namespace adoloop
