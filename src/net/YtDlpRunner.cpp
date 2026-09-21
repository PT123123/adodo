#include "YtDlpRunner.h"
#include "../app/Settings.h"
#include "../util/Subprocess.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace adoloop {

YtDlpRunner::YtDlpRunner(QObject *parent)
    : QObject(parent)
{
}

QString YtDlpRunner::findBinary() const
{
    QString yt = Settings::instance().ytDlpPath();
    if (yt.isEmpty())
        Subprocess::findExecutable(QStringLiteral("yt-dlp"), &yt);
    return yt;
}

void YtDlpRunner::resolve(const QString &url)
{
    const QString yt = findBinary();
    if (yt.isEmpty()) {
        emit failed(QStringLiteral("未找到 yt-dlp：请安装或在设置中指定路径"));
        return;
    }

    // 先 -J 取元数据；若不含直链，再 -g 兜底（用一次性连接避免重复触发）
    auto *runJson = new QProcess(this);
    runJson->setProcessChannelMode(QProcess::SeparateChannels);
    runJson->setProgram(yt);
    runJson->setArguments({QStringLiteral("-J"), QStringLiteral("--no-playlist"), url});

    connect(runJson, &QProcess::finished, this, [this, runJson, yt, url](int code, QProcess::ExitStatus) {
        runJson->deleteLater();
        if (code != 0) {
            const QString err = QString::fromUtf8(runJson->readAllStandardError()).trimmed();
            emit failed(err.isEmpty() ? QStringLiteral("yt-dlp 解析失败") : err);
            return;
        }
        const QByteArray out = runJson->readAllStandardOutput();
        QJsonParseError perr;
        const QJsonDocument doc = QJsonDocument::fromJson(out, &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
            emit failed(QStringLiteral("yt-dlp 返回解析失败"));
            return;
        }
        const QJsonObject o = doc.object();
        MediaInfo info;
        info.pathOrUrl = url;
        info.title = o.value(QStringLiteral("title")).toString();
        info.durationMs = Ms(qRound64(o.value(QStringLiteral("duration")).toDouble() * 1000.0));
        info.isOnline = true;

        QString streamUrl = o.value(QStringLiteral("url")).toString();
        if (streamUrl.isEmpty()) {
            const QJsonArray fmts = o.value(QStringLiteral("requested_formats")).toArray();
            if (!fmts.isEmpty())
                streamUrl = fmts.first().toObject().value(QStringLiteral("url")).toString();
        }
        if (!streamUrl.isEmpty()) {
            emit resolved(info, streamUrl);
            return;
        }

        // 兜底：-g 输出直链
        auto *runG = new QProcess(this);
        runG->setProcessChannelMode(QProcess::SeparateChannels);
        runG->setProgram(yt);
        runG->setArguments({QStringLiteral("-g"), QStringLiteral("--no-playlist"),
                            QStringLiteral("-f"),
                            QStringLiteral("bestaudio[ext=m4a]/bestaudio/best"), url});
        connect(runG, &QProcess::finished, this, [this, runG, info](int c2, QProcess::ExitStatus) {
            runG->deleteLater();
            if (c2 != 0) {
                emit failed(QStringLiteral("无法获取播放直链"));
                return;
            }
            const QString u = QString::fromUtf8(runG->readAllStandardOutput()).trimmed();
            if (u.isEmpty()) {
                emit failed(QStringLiteral("无法获取播放直链"));
                return;
            }
            emit resolved(info, u);
        });
        runG->start();
        if (!runG->waitForStarted(3000)) {
            emit failed(QStringLiteral("yt-dlp 启动失败: %1").arg(runG->errorString()));
        }
    });

    runJson->start();
    if (!runJson->waitForStarted(3000)) {
        emit failed(QStringLiteral("yt-dlp 启动失败: %1").arg(runJson->errorString()));
    }
}

void YtDlpRunner::downloadSubtitles(const QString &url, const QString &lang,
                                    const QString &outDir)
{
    const QString yt = findBinary();
    if (yt.isEmpty()) {
        emit failed(QStringLiteral("未找到 yt-dlp"));
        return;
    }
    QDir().mkpath(outDir);
    const QString tmpl = QDir(outDir).filePath(QStringLiteral("%(title)s.%(ext)s"));

    m_proc.setProcessChannelMode(QProcess::SeparateChannels);
    m_proc.setProgram(yt);
    m_proc.setArguments({
        QStringLiteral("--skip-download"),
        QStringLiteral("--write-subs"),
        QStringLiteral("--write-auto-subs"),
        QStringLiteral("--sub-langs"), lang.isEmpty() ? QStringLiteral("en") : lang,
        QStringLiteral("--convert-subs"), QStringLiteral("srt"),
        QStringLiteral("-o"), tmpl,
        url,
    });
    connect(&m_proc, &QProcess::finished, this, [this, outDir](int code, QProcess::ExitStatus) {
        if (code != 0) {
            const QString err = QString::fromUtf8(m_proc.readAllStandardError()).trimmed();
            emit failed(err.isEmpty() ? QStringLiteral("字幕下载失败") : err);
            return;
        }
        // 找最新生成的 srt/vtt
        QString best;
        QDateTime bestTime;
        const QDir dir(outDir);
        const auto files = dir.entryInfoList({QStringLiteral("*.srt"), QStringLiteral("*.vtt")},
                                             QDir::Files, QDir::Time);
        if (!files.isEmpty())
            best = files.first().absoluteFilePath();
        if (best.isEmpty()) {
            emit failed(QStringLiteral("未找到下载的字幕文件"));
            return;
        }
        emit subtitlesReady(best);
    });
    m_proc.start();
    if (!m_proc.waitForStarted(3000)) {
        emit failed(QStringLiteral("yt-dlp 启动失败: %1").arg(m_proc.errorString()));
    }
}

void YtDlpRunner::cancel()
{
    if (m_proc.state() != QProcess::NotRunning)
        m_proc.kill();
}

} // namespace adoloop
