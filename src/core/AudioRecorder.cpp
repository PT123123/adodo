#include "AudioRecorder.h"

#include <QAudioDevice>
#include <QIODevice>
#include <QMediaDevices>

namespace adoloop {

namespace {
constexpr int kSampleRate = 16000;
constexpr int kChannels = 1;
constexpr int kBitsPerSample = 16;
constexpr qint64 kHeaderSize = 44;

// Qt6：QAudioSource 在构造时确定格式（无 setFormat），故先选出设备支持的格式。
// 优先 16kHz 单声道 16bit；设备不支持时退回设备首选格式（尽量接近）。
QAudioFormat pickInputFormat()
{
    QAudioFormat fmt;
    fmt.setSampleRate(kSampleRate);
    fmt.setChannelCount(kChannels);
    fmt.setSampleFormat(QAudioFormat::Int16);

    const QAudioDevice dev = QMediaDevices::defaultAudioInput();
    if (dev.isFormatSupported(fmt))
        return fmt;
    return dev.preferredFormat();
}

QByteArray buildWavHeader(qint64 dataBytes)
{
    QByteArray h(44, '\0');
    const quint32 dataLen = quint32(dataBytes);
    const quint32 byteRate = quint32(kSampleRate * kChannels * kBitsPerSample / 8);
    const quint16 blockAlign = quint16(kChannels * kBitsPerSample / 8);
    const quint32 totalLen = 36 + dataLen;

    auto put = [&](qint64 off, quint32 v) {
        h[off] = char(v & 0xFF);
        h[off + 1] = char((v >> 8) & 0xFF);
        h[off + 2] = char((v >> 16) & 0xFF);
        h[off + 3] = char((v >> 24) & 0xFF);
    };
    auto put16 = [&](qint64 off, quint16 v) {
        h[off] = char(v & 0xFF);
        h[off + 1] = char((v >> 8) & 0xFF);
    };
    memcpy(h.data(), "RIFF", 4);
    put(4, totalLen);
    memcpy(h.data() + 8, "WAVE", 4);
    memcpy(h.data() + 12, "fmt ", 4);
    put(16, 16);
    put16(20, 1); // PCM
    put16(22, quint16(kChannels));
    put(24, quint32(kSampleRate));
    put(28, byteRate);
    put16(32, blockAlign);
    put16(34, quint16(kBitsPerSample));
    memcpy(h.data() + 36, "data", 4);
    put(40, dataLen);
    return h;
}

// 内存接收 QIODevice：把音频数据追加到 QByteArray
class BufferDevice : public QIODevice {
public:
    explicit BufferDevice(QByteArray *buf)
        : m_buf(buf)
    {
        open(QIODevice::WriteOnly | QIODevice::Unbuffered);
    }
    qint64 readData(char *, qint64) override { return 0; }
    qint64 writeData(const char *data, qint64 len) override
    {
        m_buf->append(data, int(len));
        return len;
    }
    bool isSequential() const override { return true; }

private:
    QByteArray *m_buf;
};
} // namespace

AudioRecorder::AudioRecorder(QObject *parent)
    : QObject(parent)
    , m_source(QMediaDevices::defaultAudioInput(), pickInputFormat())
{
}

AudioRecorder::~AudioRecorder()
{
    if (m_recording)
        m_source.stop();
    delete m_bufferDevice;
    m_bufferDevice = nullptr;
}

bool AudioRecorder::inputAvailable()
{
    // 无输入设备（无麦克风）时 defaultAudioInput() 为空设备
    return !QMediaDevices::defaultAudioInput().isNull();
}

bool AudioRecorder::start(const QString &destWavPath)
{
    if (m_recording)
        stop();
    if (destWavPath.isEmpty())
        return false;

    m_file.setFileName(destWavPath);
    if (!m_file.open(QIODevice::WriteOnly)) {
        emit failed(m_file.errorString());
        return false;
    }
    // 占位 WAV 头，stop 时回填
    m_file.write(buildWavHeader(0));
    m_bytes = 0;
    m_filePath = destWavPath;
    m_buffered = false;

    m_source.start(&m_file);
    if (m_source.error() != QAudio::NoError) {
        m_file.close();
        emit failed(QStringLiteral("无法启动录音设备"));
        return false;
    }
    m_recording = true;
    emit started();
    return true;
}

void AudioRecorder::stop()
{
    if (!m_recording)
        return;
    m_source.stop();
    m_recording = false;

    // 实际写入的数据字节（除去 44 字节头）
    m_bytes = qMax<qint64>(0, m_file.size() - kHeaderSize);

    // 回填 WAV 头
    m_file.seek(0);
    m_file.write(buildWavHeader(m_bytes));
    m_file.close();

    const Ms ms = recordedMs();
    emit stopped(ms);
}

bool AudioRecorder::startToBuffer()
{
    if (m_recording)
        stopToBuffer();
    m_buffer.clear();
    m_buffered = true;
    m_filePath.clear();

    auto *dev = new BufferDevice(&m_buffer);
    m_bufferDevice = dev;
    m_source.start(dev);
    if (m_source.error() != QAudio::NoError) {
        m_buffered = false;
        delete dev;
        m_bufferDevice = nullptr;
        emit failed(QStringLiteral("无法启动录音设备"));
        return false;
    }
    m_recording = true;
    emit started();
    return true;
}

QByteArray AudioRecorder::stopToBuffer()
{
    if (!m_recording)
        return QByteArray();
    m_source.stop();
    m_recording = false;
    delete m_bufferDevice;
    m_bufferDevice = nullptr;
    const QByteArray out = m_buffer;
    m_buffer.clear();
    m_buffered = false;
    emit stopped(recordedMs());
    return out;
}

QByteArray AudioRecorder::drainBuffer()
{
    QByteArray out = m_buffer;
    m_buffer.clear();
    return out;
}

Ms AudioRecorder::recordedMs() const
{
    if (!m_buffered) {
        if (m_bytes <= 0)
            return 0;
        const int sr = m_source.format().sampleRate();
        const int ch = m_source.format().channelCount();
        const int bytesPerSample = m_source.format().bytesPerSample();
        const qint64 samples = m_bytes / qMax(1, ch * bytesPerSample);
        return Ms(samples * 1000 / qMax(1, sr));
    }
    if (m_buffer.isEmpty())
        return 0;
    const int sr = m_source.format().sampleRate();
    const int ch = m_source.format().channelCount();
    const int bytesPerSample = m_source.format().bytesPerSample();
    const qint64 samples = m_buffer.size() / qMax(1, ch * bytesPerSample);
    return Ms(samples * 1000 / qMax(1, sr));
}

} // namespace adoloop
