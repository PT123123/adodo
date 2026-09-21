#pragma once

#include "../core/Types.h"

#include <QKeySequence>
#include <QMap>
#include <QObject>
#include <QString>

class QSettings;

namespace adoloop {

// 全局设置：QSettings(INI) 持久化，单例。
// 覆盖：数据目录、播放、断句参数、ASR 引擎、外部工具路径、快捷键、学习默认值。
class Settings : public QObject {
    Q_OBJECT
public:
    static Settings &instance();
    static void initialize(const QString &dataDirOverride = QString());

    // ---------- 数据目录 ----------
    QString dataDir() const;
    void setDataDir(const QString &dir);
    QString cacheDir() const;      // dataDir/cache
    QString recordingsDir() const; // dataDir/recordings
    QString dictationsDir() const; // dataDir/dictations
    void ensureDirs() const;

    // ---------- 播放 ----------
    double defaultRate() const;
    void setDefaultRate(double v);
    int volume() const;
    void setVolume(int v);
    // 断点续播：再次打开文件时自动跳到上次位置（关闭时仍保留记录，只是不跳转）
    bool resumeEnabled() const;
    void setResumeEnabled(bool v);

    // ---------- 外部工具（空=自动在 PATH 中找） ----------
    QString mpvPath() const;
    void setMpvPath(const QString &p);
    QString ffmpegPath() const;
    void setFfmpegPath(const QString &p);
    QString ytDlpPath() const;
    void setYtDlpPath(const QString &p);
    QString whisperCliPath() const;
    void setWhisperCliPath(const QString &p);
    QString pythonPath() const;
    void setPythonPath(const QString &p);
    QString whisperModelPath() const;
    void setWhisperModelPath(const QString &p);
    // 日语分词增强（可选装；留空 = 用内置启发式分词）
    QString mecabPath() const;
    void setMecabPath(const QString &p);

    // ---------- ASR ----------
    QString asrEngine() const; // "whisper.cpp" | "faster-whisper"
    void setAsrEngine(const QString &e);
    // ASR 语言：默认「跟随学习语言」，也可手动指定
    bool asrLangFollowsStudy() const;
    void setAsrLangFollowsStudy(bool v);
    QString asrLanguage() const;       // 生效值（跟随时 = studyLanguage()）
    void setAsrLanguage(const QString &lang);
    QString asrLanguageManual() const; // 手动指定值（设置界面回显）

    // ---------- 断句参数 ----------
    SegmentParams segmentParams() const;
    void setSegmentParams(const SegmentParams &p);

    // ---------- 学习默认值 ----------
    int repeatCount() const;
    void setRepeatCount(int n);
    int repeatGapMs() const;
    void setRepeatGapMs(int ms);
    bool dictationIgnorePunctuation() const;
    void setDictationIgnorePunctuation(bool v);
    bool dictationIgnoreCase() const;
    void setDictationIgnoreCase(bool v);
    // 学习语言："ja"（默认）| "en"；源语言与 ASR 语言默认跟随它
    QString studyLanguage() const;
    void setStudyLanguage(const QString &l);
    QString sourceLang() const; // 默认跟随学习语言（已有 ini 值不被覆盖）
    void setSourceLang(const QString &l);
    QString targetLang() const;
    void setTargetLang(const QString &l);

    // ---------- 字幕取词（M11） ----------
    // 悬停多久触发查词（毫秒，0 = 只在点击时查词）
    int dictHoverDelayMs() const;
    void setDictHoverDelayMs(int ms);
    // 「仅点击时查词」开关：false（默认）= 悬停也查
    bool dictHoverLookupEnabled() const;
    void setDictHoverLookupEnabled(bool v);

    // ---------- 段落复述（M12） ----------
    // 段落范围的句数（连续 N 句一段）
    int retellParagraphSize() const;
    void setRetellParagraphSize(int n);
    // 复述时是否录音（关掉 = 静默回忆模式；无麦克风时自动降级）
    bool retellRecordEnabled() const;
    void setRetellRecordEnabled(bool v);

    // ---------- 快捷键 ----------
    QKeySequence keySequence(const QString &actionId) const;
    void setKeySequence(const QString &actionId, const QKeySequence &seq);
    QMap<QString, QKeySequence> allKeyBindings() const;
    void resetKeyBindings();
    // 默认键位表（新安装/恢复默认用）
    static QMap<QString, QKeySequence> defaultKeyBindings();
    // 动作中文名（设置界面显示）
    static QString actionDisplayName(const QString &actionId);

private:
    explicit Settings(QObject *parent = nullptr);
    QSettings *m_settings = nullptr;
};

} // namespace adoloop
