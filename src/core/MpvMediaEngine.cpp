#include "MpvMediaEngine.h"
#include "../app/Settings.h"
#include "../util/Subprocess.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QThread>

namespace adoloop {

MpvMediaEngine::MpvMediaEngine(QObject *parent)
    : MediaEngine(parent)
{
    m_volume = Settings::instance().volume();
    m_rate = Settings::instance().defaultRate();

    m_pipeName = QStringLiteral("adoloop-mpv-%1").arg(QCoreApplication::applicationPid());

    connect(&m_socket, &QLocalSocket::connected, this, [this]() {
        m_reconnectTimer.stop();
        m_lastError.clear();
        observeProperties();
        if (!m_pendingSource.isEmpty()) {
            const QString src = m_pendingSource;
            m_pendingSource.clear();
            open(src);
        }
    });
    connect(&m_socket, &QLocalSocket::readyRead, this, [this]() {
        while (m_socket.canReadLine()) {
            const QByteArray line = m_socket.readLine().trimmed();
            if (!line.isEmpty())
                handleMessage(line);
        }
    });
    connect(&m_socket, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) {
        // 连接失败/断开：稍后重连（mpv 仍在运行）
        if (!m_quitting && m_proc.state() == QProcess::Running) {
            m_reconnectTimer.start(300);
        }
    });
    connect(&m_reconnectTimer, &QTimer::timeout, this, [this]() {
        m_reconnectTimer.stop();
        if (m_socket.state() != QLocalSocket::ConnectedState) {
            m_socket.abort();
            m_socket.connectToServer(m_pipeName);
        }
    });
    connect(&m_proc, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        onProcessExit();
    });

    startMpv();
}

MpvMediaEngine::~MpvMediaEngine()
{
    m_quitting = true;
    m_reconnectTimer.stop();
    if (m_socket.state() == QLocalSocket::ConnectedState) {
        // 优雅退出
        sendCommand({QStringLiteral("quit")});
        m_socket.waitForBytesWritten(200);
    }
    m_socket.abort();
    if (m_proc.state() != QProcess::NotRunning) {
        m_proc.kill();
        m_proc.waitForFinished(1000);
    }
}

void MpvMediaEngine::startMpv()
{
    QString mpv = Settings::instance().mpvPath();
    if (mpv.isEmpty()) {
        QString found;
        if (Subprocess::findExecutable(QStringLiteral("mpv"), &found))
            mpv = found;
    }
    if (mpv.isEmpty()) {
        m_lastError = QStringLiteral("未找到 mpv.exe：请安装 mpv 或在设置中指定路径（将回退 Qt 播放后端）");
        QTimer::singleShot(0, this, [this]() { emit errorOccurred(m_lastError); });
        return;
    }

    QString ipcArg;
#ifdef Q_OS_WIN
    ipcArg = QStringLiteral("\\\\.\\pipe\\%1").arg(m_pipeName);
#else
    ipcArg = QDir::temp().filePath(m_pipeName);
#endif

    QStringList args = {
        QStringLiteral("--no-config"),
        QStringLiteral("--idle=yes"),
        QStringLiteral("--keep-open=yes"),
        QStringLiteral("--force-window=yes"),
        QStringLiteral("--audio-pitch-correction=yes"),
        QStringLiteral("--input-ipc-server=%1").arg(ipcArg),
        QStringLiteral("--volume=%1").arg(m_volume),
    };

    m_proc.setProgram(mpv);
    m_proc.setArguments(args);
    m_proc.start();

    if (!m_proc.waitForStarted(3000)) {
        m_lastError = QStringLiteral("mpv 启动失败: %1").arg(m_proc.errorString());
        emit errorOccurred(m_lastError);
        return;
    }

    m_socket.connectToServer(m_pipeName);
    m_reconnectTimer.start(300);
}

void MpvMediaEngine::sendCommand(const QVariantList &args)
{
    if (m_socket.state() != QLocalSocket::ConnectedState)
        return;
    QJsonArray arr;
    for (const QVariant &v : args)
        arr.append(QJsonValue::fromVariant(v));
    QJsonObject obj;
    obj.insert(QStringLiteral("command"), arr);
    m_socket.write(QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n');
}

void MpvMediaEngine::sendSet(const QString &prop, const QVariant &value)
{
    sendCommand({QStringLiteral("set-property"), prop, value});
}

void MpvMediaEngine::observeProperties()
{
    sendCommand({QStringLiteral("observe_property"), 1, QStringLiteral("time-pos")});
    sendCommand({QStringLiteral("observe_property"), 2, QStringLiteral("duration")});
    sendCommand({QStringLiteral("observe_property"), 3, QStringLiteral("pause")});
    sendCommand({QStringLiteral("observe_property"), 4, QStringLiteral("eof-reached")});
    sendCommand({QStringLiteral("observe_property"), 5, QStringLiteral("idle-active")});
}

void MpvMediaEngine::handleMessage(const QByteArray &line)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return;
    const QJsonObject o = doc.object();

    if (o.contains(QStringLiteral("event"))) {
        const QString event = o.value(QStringLiteral("event")).toString();
        if (event == QStringLiteral("property-change")) {
            const QString name = o.value(QStringLiteral("name")).toString();
            const QJsonValue data = o.value(QStringLiteral("data"));
            if (name == QStringLiteral("time-pos")) {
                if (data.isDouble()) {
                    m_position = Ms(qRound64(data.toDouble() * 1000.0));
                    emit positionChanged(m_position);
                }
            } else if (name == QStringLiteral("duration")) {
                if (data.isDouble()) {
                    m_duration = Ms(qRound64(data.toDouble() * 1000.0));
                    emit durationChanged(m_duration);
                }
            } else if (name == QStringLiteral("pause")) {
                const bool paused = data.toBool(false);
                const bool was = m_playing;
                m_playing = !paused && m_loaded;
                if (was != m_playing)
                    emit playStateChanged(m_playing);
            } else if (name == QStringLiteral("eof-reached")) {
                if (data.toBool(false))
                    emit eofReached();
            } else if (name == QStringLiteral("idle-active")) {
                const bool loaded = !data.toBool(true);
                if (loaded != m_loaded) {
                    m_loaded = loaded;
                    emit loadedChanged(m_loaded);
                    if (m_loaded)
                        emit playStateChanged(m_playing);
                }
            }
        } else if (event == QStringLiteral("end-file")) {
            if (o.value(QStringLiteral("reason")).toString() == QStringLiteral("eof"))
                emit eofReached();
        } else if (event == QStringLiteral("playback-restart")) {
            // 播放重开，主动刷新一次位置
            sendCommand({QStringLiteral("get_property"), QStringLiteral("time-pos")});
        }
        return;
    }

    // 命令回复：{"request_id": N, "error": "success"}；error 非 success 时报错
    if (o.contains(QStringLiteral("error")) && o.contains(QStringLiteral("request_id"))) {
        const QString e = o.value(QStringLiteral("error")).toString();
        if (e != QStringLiteral("success")) {
            m_lastError = QStringLiteral("mpv: %1").arg(e);
            emit errorOccurred(m_lastError);
        }
    }
}

void MpvMediaEngine::onProcessExit()
{
    const bool wasLoaded = m_loaded;
    m_loaded = false;
    m_playing = false;
    m_reconnectTimer.stop();
    if (!m_quitting) {
        m_lastError = QStringLiteral("mpv 进程退出: %1").arg(m_proc.errorString());
        emit errorOccurred(m_lastError);
    }
    if (wasLoaded) {
        emit loadedChanged(false);
        emit playStateChanged(false);
    }
}

bool MpvMediaEngine::open(const QString &source)
{
    if (source.isEmpty())
        return false;
    if (m_socket.state() != QLocalSocket::ConnectedState) {
        m_pendingSource = source;
        return true; // 连接建立后自动加载
    }
    sendCommand({QStringLiteral("loadfile"), source, QStringLiteral("replace")});
    m_loaded = false;
    emit loadedChanged(false);
    return true;
}

void MpvMediaEngine::play()
{
    if (m_loaded)
        sendSet(QStringLiteral("pause"), false);
}

void MpvMediaEngine::pause()
{
    if (m_loaded)
        sendSet(QStringLiteral("pause"), true);
}

void MpvMediaEngine::stop()
{
    sendCommand({QStringLiteral("stop")});
    m_loaded = false;
    m_playing = false;
    emit loadedChanged(false);
    emit playStateChanged(false);
}

bool MpvMediaEngine::seek(Ms pos)
{
    if (!m_loaded)
        return false;
    sendCommand({QStringLiteral("seek"), double(pos) / 1000.0, QStringLiteral("absolute+exact")});
    m_position = qMax<Ms>(0, pos);
    emit positionChanged(m_position);
    return true;
}

void MpvMediaEngine::setRate(double rate)
{
    m_rate = qBound(0.25, rate, 2.0);
    if (m_loaded)
        sendSet(QStringLiteral("speed"), m_rate);
}

void MpvMediaEngine::setVolume(int volume)
{
    m_volume = qBound(0, volume, 100);
    if (m_loaded)
        sendSet(QStringLiteral("volume"), double(m_volume));
}

} // namespace adoloop
