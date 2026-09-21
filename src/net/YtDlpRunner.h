#pragma once

#include "../core/Types.h"

#include <QObject>
#include <QProcess>
#include <QString>

namespace adoloop {

// yt-dlp 集成：在线播放解析（拉直链）+ 在线字幕下载。
// 依赖 yt-dlp 可执行文件；缺失时 resolve 发 failed 提示。
class YtDlpRunner : public QObject {
    Q_OBJECT
public:
    explicit YtDlpRunner(QObject *parent = nullptr);

    // 解析 URL → MediaInfo（含直链 streamUrl，供播放引擎加载）
    void resolve(const QString &url);

    // 下载自动字幕到 outDir，成功后回传本地文件路径
    void downloadSubtitles(const QString &url, const QString &lang,
                           const QString &outDir);

    void cancel();

signals:
    void resolved(const adoloop::MediaInfo &info, const QString &streamUrl);
    void subtitlesReady(const QString &localPath);
    void failed(const QString &error);

private:
    QString findBinary() const;

    QProcess m_proc;
};

} // namespace adoloop
