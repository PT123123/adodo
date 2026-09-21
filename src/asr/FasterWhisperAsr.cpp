#include "FasterWhisperAsr.h"
#include "../app/Settings.h"
#include "../util/Subprocess.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace adoloop {

namespace {
const QString kResultKey = QStringLiteral("result");
const QString kProgressKey = QStringLiteral("progress");
}

FasterWhisperAsr::FasterWhisperAsr(QObject *parent)
    : AsrEngine(parent)
{
    connect(&m_proc, &QProcess::readyReadStandardOutput, this, [this]() {
        while (m_proc.canReadLine())
            onLine(m_proc.readLine().trimmed());
    });
    connect(&m_proc, &QProcess::finished, this,
            [this](int code, QProcess::ExitStatus) { onFinished(code); });
}

QString FasterWhisperAsr::bridgeScriptPath()
{
    // 1) 源码树：<root>/scripts/faster_whisper_bridge.py
    // 2) 可执行目录：<exeDir>/scripts/faster_whisper_bridge.py
    const QStringList candidates = {
        QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("scripts/faster_whisper_bridge.py")),
        QDir::current().filePath(QStringLiteral("scripts/faster_whisper_bridge.py")),
    };
    for (const QString &c : candidates) {
        if (QFileInfo::exists(c))
            return c;
    }
    return candidates.first();
}

bool FasterWhisperAsr::isAvailable(QString *reason) const
{
    QString py = Settings::instance().pythonPath();
    if (py.isEmpty())
        Subprocess::findExecutable(QStringLiteral("python"), &py);
    if (py.isEmpty() && reason)
        *reason = QStringLiteral("未找到 python（需要 faster-whisper 环境）");
    if (!py.isEmpty() && reason)
        reason->clear();
    return !py.isEmpty();
}

void FasterWhisperAsr::start(const QString &mediaPath, const Options &opt)
{
    QString py = Settings::instance().pythonPath();
    if (py.isEmpty())
        Subprocess::findExecutable(QStringLiteral("python"), &py);
    if (py.isEmpty()) {
        emit failed(QStringLiteral("未找到 python"));
        return;
    }
    const QString bridge = bridgeScriptPath();
    if (!QFileInfo::exists(bridge)) {
        emit failed(QStringLiteral("缺少桥脚本: %1").arg(bridge));
        return;
    }
    if (!QFileInfo::exists(mediaPath)) {
        emit failed(QStringLiteral("媒体文件不存在: %1").arg(mediaPath));
        return;
    }

    QStringList args = {bridge, mediaPath};
    if (!opt.model.isEmpty())
        args << QStringLiteral("--model") << opt.model;
    if (!opt.language.isEmpty() && opt.language != QLatin1String("auto"))
        args << QStringLiteral("--language") << opt.language;
    args << QStringLiteral("--beam") << QString::number(qMax(1, opt.beamSize));
    if (!opt.outputDir.isEmpty()) {
        QDir().mkpath(opt.outputDir);
        args << QStringLiteral("--cache") << opt.outputDir;
    }

    m_proc.setProgram(py);
    m_proc.setArguments(args);
    m_proc.start();
    if (!m_proc.waitForStarted(3000)) {
        emit failed(QStringLiteral("python 启动失败: %1").arg(m_proc.errorString()));
        return;
    }
    emit progress(0.0f, QStringLiteral("加载模型…"));
}

void FasterWhisperAsr::cancel()
{
    if (m_proc.state() != QProcess::NotRunning)
        m_proc.kill();
    emit cancelled();
}

void FasterWhisperAsr::onLine(const QByteArray &line)
{
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject())
        return;
    const QJsonObject o = doc.object();
    if (o.contains(kProgressKey)) {
        emit progress(float(o.value(kProgressKey).toDouble()), QStringLiteral("转写中…"));
        return;
    }
    if (o.contains(kResultKey)) {
        const QJsonArray arr = o.value(kResultKey).toArray();
        QVector<AsrSegment> segments;
        segments.reserve(arr.size());
        for (const QJsonValue &v : arr) {
            const QJsonObject seg = v.toObject();
            AsrSegment s;
            s.start = Ms(qRound64(seg.value(QStringLiteral("start")).toDouble()));
            s.end = Ms(qRound64(seg.value(QStringLiteral("end")).toDouble()));
            s.text = seg.value(QStringLiteral("text")).toString().trimmed();
            const QJsonArray ws = seg.value(QStringLiteral("words")).toArray();
            for (const QJsonValue &wv : ws) {
                const QJsonObject wo = wv.toObject();
                WordToken t;
                t.word = wo.value(QStringLiteral("word")).toString().trimmed();
                t.start = Ms(qRound64(wo.value(QStringLiteral("start")).toDouble()));
                t.end = Ms(qRound64(wo.value(QStringLiteral("end")).toDouble()));
                if (!t.word.isEmpty())
                    s.words << t;
            }
            if (s.end > s.start && !s.text.isEmpty())
                segments << s;
        }
        emit finished(segments);
    }
}

void FasterWhisperAsr::onFinished(int exitCode)
{
    if (exitCode != 0) {
        const QString err = QString::fromUtf8(m_proc.readAllStandardError()).trimmed();
        emit failed(err.isEmpty() ? QStringLiteral("faster-whisper 退出码 %1").arg(exitCode) : err);
    }
}

} // namespace adoloop
