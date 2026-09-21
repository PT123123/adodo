#include "WhisperCppAsr.h"
#include "../app/Settings.h"
#include "../util/Subprocess.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace adoloop {

WhisperCppAsr::WhisperCppAsr(QObject *parent)
    : AsrEngine(parent)
{
    connect(&m_proc, &QProcess::finished, this,
            [this](int code, QProcess::ExitStatus) { onFinished(code); });
}

bool WhisperCppAsr::isAvailable(QString *reason) const
{
    QString cli = Settings::instance().whisperCliPath();
    if (cli.isEmpty())
        Subprocess::findExecutable(QStringLiteral("whisper-cli"), &cli);
    if (cli.isEmpty() && reason)
        *reason = QStringLiteral("未找到 whisper-cli（whisper.cpp 构建产物）");
    if (!cli.isEmpty() && reason)
        reason->clear();
    return !cli.isEmpty();
}

void WhisperCppAsr::start(const QString &mediaPath, const Options &opt)
{
    QString cli = Settings::instance().whisperCliPath();
    if (cli.isEmpty())
        Subprocess::findExecutable(QStringLiteral("whisper-cli"), &cli);
    if (cli.isEmpty()) {
        emit failed(QStringLiteral("未找到 whisper-cli"));
        return;
    }
    QString model = opt.model;
    if (model.isEmpty())
        model = Settings::instance().whisperModelPath();
    if (model.isEmpty()) {
        emit failed(QStringLiteral("未配置 whisper 模型路径（设置 → ASR）"));
        return;
    }
    if (!QFileInfo::exists(mediaPath)) {
        emit failed(QStringLiteral("媒体文件不存在: %1").arg(mediaPath));
        return;
    }

    QDir().mkpath(opt.outputDir);
    m_outputBase = QDir(opt.outputDir).filePath(QFileInfo(mediaPath).completeBaseName()
                                                + QStringLiteral(".asr"));

    QStringList args = {
        QStringLiteral("-m"), model,
        QStringLiteral("-f"), mediaPath,
        QStringLiteral("-oj"),
        QStringLiteral("-of"), m_outputBase,
        QStringLiteral("-s"),       // split on word（尽量输出词级时间戳）
        QStringLiteral("-pp"),      // 进度输出到 stderr
    };
    if (!opt.language.isEmpty() && opt.language != QLatin1String("auto"))
        args << QStringLiteral("-l") << opt.language;

    m_proc.setProgram(cli);
    m_proc.setArguments(args);
    m_proc.start();
    if (!m_proc.waitForStarted(3000)) {
        emit failed(QStringLiteral("whisper-cli 启动失败: %1").arg(m_proc.errorString()));
        return;
    }
    emit progress(0.0f, QStringLiteral("转写中…"));
}

void WhisperCppAsr::cancel()
{
    if (m_proc.state() != QProcess::NotRunning)
        m_proc.kill();
    emit cancelled();
}

void WhisperCppAsr::onFinished(int exitCode)
{
    if (exitCode != 0) {
        const QString err = QString::fromUtf8(m_proc.readAllStandardError()).trimmed();
        emit failed(err.isEmpty() ? QStringLiteral("whisper-cli 退出码 %1").arg(exitCode) : err);
        return;
    }
    const QString jsonPath = m_outputBase + QStringLiteral(".json");
    parseJson(jsonPath);
}

void WhisperCppAsr::parseJson(const QString &jsonPath)
{
    QFile f(jsonPath);
    if (!f.open(QIODevice::ReadOnly)) {
        emit failed(QStringLiteral("无法读取 ASR 结果: %1").arg(f.errorString()));
        return;
    }
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    f.close();
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        emit failed(QStringLiteral("ASR 结果 JSON 解析失败"));
        return;
    }

    const QJsonArray arr = doc.object().value(QStringLiteral("transcription")).toArray();
    QVector<AsrSegment> segments;
    segments.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        const QJsonObject seg = v.toObject();
        const QJsonObject offsets = seg.value(QStringLiteral("offsets")).toObject();
        AsrSegment s;
        s.start = Ms(qRound64(offsets.value(QStringLiteral("from")).toDouble()));
        s.end = Ms(qRound64(offsets.value(QStringLiteral("to")).toDouble()));
        s.text = seg.value(QStringLiteral("text")).toString().trimmed();
        const QJsonArray ws = seg.value(QStringLiteral("words")).toArray();
        for (const QJsonValue &wv : ws) {
            const QJsonObject wo = wv.toObject();
            const QJsonObject woffs = wo.value(QStringLiteral("offsets")).toObject();
            WordToken t;
            t.word = wo.value(QStringLiteral("word")).toString().trimmed();
            t.start = Ms(qRound64(woffs.value(QStringLiteral("from")).toDouble()));
            t.end = Ms(qRound64(woffs.value(QStringLiteral("to")).toDouble()));
            if (!t.word.isEmpty())
                s.words << t;
        }
        if (s.end > s.start && !s.text.isEmpty())
            segments << s;
    }
    emit finished(segments);
}

} // namespace adoloop
