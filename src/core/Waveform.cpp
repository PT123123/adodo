#include "Waveform.h"
#include "../app/Settings.h"
#include "../util/Subprocess.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFileInfo>

namespace adoloop {

namespace {
const quint32 kWaveMagic = 0x41445750; // "ADWP"
constexpr int kSampleRate = 8000;
constexpr int kSamplesPerBlock = kSampleRate * Waveform::kBlockMs / 1000; // 80
}

Waveform::Waveform(QObject *parent)
    : QObject(parent)
{
}

QString waveformCachePathFor(const QString &mediaPath, const QString &cacheDir)
{
    const QByteArray hash = QCryptographicHash::hash(mediaPath.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QDir(cacheDir).filePath(QString::fromLatin1(hash) + QStringLiteral(".wvpk"));
}

bool Waveform::loadCache(const QString &cachePath)
{
    QFile f(cachePath);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QDataStream in(&f);
    in.setByteOrder(QDataStream::LittleEndian);
    quint32 magic = 0;
    qint64 totalMs = 0;
    qint32 count = 0;
    in >> magic >> totalMs >> count;
    if (magic != kWaveMagic || count <= 0 || count > 10000000)
        return false;
    m_min.resize(count);
    m_max.resize(count);
    for (int i = 0; i < count; ++i)
        in >> m_min[i];
    for (int i = 0; i < count; ++i)
        in >> m_max[i];
    m_totalMs = Ms(totalMs);
    emit ready();
    return true;
}

void Waveform::generate(const QString &mediaPath, const QString &cacheDir)
{
    if (m_running)
        return;
    if (!QFileInfo::exists(mediaPath)) {
        emit failed(QStringLiteral("文件不存在: %1").arg(mediaPath));
        return;
    }

    QDir().mkpath(cacheDir);
    const QString cachePath = waveformCachePathFor(mediaPath, cacheDir);
    if (loadCache(cachePath))
        return; // 命中缓存

    QString ffmpeg = Settings::instance().ffmpegPath();
    if (ffmpeg.isEmpty())
        Subprocess::findExecutable(QStringLiteral("ffmpeg"), &ffmpeg);
    if (ffmpeg.isEmpty()) {
        emit failed(QStringLiteral("未找到 ffmpeg：无法生成波形（不影响播放）"));
        return;
    }

    m_running = true;
    m_pcm.clear();
    m_min.clear();
    m_max.clear();
    m_totalMs = 0;

    QStringList args = {
        QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-i"), mediaPath,
        QStringLiteral("-f"), QStringLiteral("s16le"),
        QStringLiteral("-ac"), QStringLiteral("1"),
        QStringLiteral("-ar"), QStringLiteral("8000"),
        QStringLiteral("-"), // stdout
    };

    m_proc.setProcessChannelMode(QProcess::SeparateChannels);
    m_proc.setProgram(ffmpeg);
    m_proc.setArguments(args);

    connect(&m_proc, &QProcess::readyReadStandardOutput, this, [this]() {
        const QByteArray chunk = m_proc.readAllStandardOutput();
        if (!chunk.isEmpty())
            m_pcm.append(chunk);
    });
    connect(&m_proc, &QProcess::finished, this, [this, cachePath](int code, QProcess::ExitStatus) {
        m_running = false;
        if (code != 0) {
            const QString err = QString::fromUtf8(m_proc.readAllStandardError()).trimmed();
            emit failed(err.isEmpty() ? QStringLiteral("ffmpeg 解码失败") : err);
            return;
        }
        // 8kHz 16bit mono → 时长
        const Ms totalMs = Ms(m_pcm.size() / 2) * 1000 / kSampleRate;
        processPcm(m_pcm, totalMs);
        finish();

        // 写缓存
        QFile f(cachePath);
        if (f.open(QIODevice::WriteOnly)) {
            QDataStream out(&f);
            out.setByteOrder(QDataStream::LittleEndian);
            out << kWaveMagic << qint64(m_totalMs) << qint32(m_min.size());
            for (float v : m_min)
                out << v;
            for (float v : m_max)
                out << v;
            f.close();
        }
        emit ready();
    });

    m_proc.start();
    if (!m_proc.waitForStarted(3000)) {
        m_running = false;
        emit failed(QStringLiteral("ffmpeg 启动失败: %1").arg(m_proc.errorString()));
    }
}

void Waveform::processPcm(const QByteArray &pcm, Ms totalMs)
{
    const int n = pcm.size() / 2;
    const int blocks = qMax(1, (n + kSamplesPerBlock - 1) / kSamplesPerBlock);
    m_min.resize(blocks);
    m_max.resize(blocks);
    for (int b = 0; b < blocks; ++b) {
        float mn = 1.0f, mx = -1.0f;
        const int begin = b * kSamplesPerBlock;
        const int end = qMin(n, begin + kSamplesPerBlock);
        for (int i = begin; i < end; ++i) {
            qint16 s = 0;
            memcpy(&s, pcm.constData() + i * 2, 2);
            const float v = float(s) / 32768.0f;
            mn = qMin(mn, v);
            mx = qMax(mx, v);
        }
        m_min[b] = mn;
        m_max[b] = mx;
    }
    m_totalMs = qMax<Ms>(1, totalMs);
}

void Waveform::finish()
{
    m_pcm.clear();
}

} // namespace adoloop
