#include "Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QHash>
#include <QSettings>
#include <QStandardPaths>

namespace adoloop {

namespace {
const char *kGroup = "adoloop";
}

Settings &Settings::instance()
{
    static Settings s;
    return s;
}

void Settings::initialize(const QString &dataDirOverride)
{
    Settings &s = Settings::instance();
    if (dataDirOverride.isEmpty()) {
        const QString def = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                                .filePath(QStringLiteral("AdoLoop"));
        if (s.m_settings->value("dataDir").toString().isEmpty())
            s.m_settings->setValue("dataDir", def);
    } else {
        s.m_settings->setValue("dataDir", dataDirOverride);
    }
    s.ensureDirs();
}

Settings::Settings(QObject *parent)
    : QObject(parent)
{
    const QString iniDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(iniDir);
    m_settings = new QSettings(QDir(iniDir).filePath(QStringLiteral("adoloop.ini")),
                               QSettings::IniFormat, this);
}

QString Settings::dataDir() const
{
    QString d = m_settings->value("dataDir").toString();
    if (d.isEmpty())
        d = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                .filePath(QStringLiteral("AdoLoop"));
    return d;
}

void Settings::setDataDir(const QString &dir)
{
    if (dir.isEmpty())
        return;
    m_settings->setValue("dataDir", QDir::toNativeSeparators(dir));
    ensureDirs();
}

QString Settings::cacheDir() const { return QDir(dataDir()).filePath(QStringLiteral("cache")); }
QString Settings::recordingsDir() const { return QDir(dataDir()).filePath(QStringLiteral("recordings")); }
QString Settings::dictationsDir() const { return QDir(dataDir()).filePath(QStringLiteral("dictations")); }

void Settings::ensureDirs() const
{
    QDir().mkpath(dataDir());
    QDir().mkpath(cacheDir());
    QDir().mkpath(recordingsDir());
    QDir().mkpath(dictationsDir());
}

double Settings::defaultRate() const { return m_settings->value("play/defaultRate", 1.0).toDouble(); }
void Settings::setDefaultRate(double v) { m_settings->setValue("play/defaultRate", v); }
int Settings::volume() const { return m_settings->value("play/volume", 80).toInt(); }
void Settings::setVolume(int v) { m_settings->setValue("play/volume", qBound(0, v, 100)); }
bool Settings::resumeEnabled() const { return m_settings->value("play/resumeEnabled", true).toBool(); }
void Settings::setResumeEnabled(bool v) { m_settings->setValue("play/resumeEnabled", v); }

QString Settings::mpvPath() const { return m_settings->value("tools/mpv").toString(); }
void Settings::setMpvPath(const QString &p) { m_settings->setValue("tools/mpv", p); }
QString Settings::ffmpegPath() const { return m_settings->value("tools/ffmpeg").toString(); }
void Settings::setFfmpegPath(const QString &p) { m_settings->setValue("tools/ffmpeg", p); }
QString Settings::ytDlpPath() const { return m_settings->value("tools/ytdlp").toString(); }
void Settings::setYtDlpPath(const QString &p) { m_settings->setValue("tools/ytdlp", p); }
QString Settings::whisperCliPath() const { return m_settings->value("tools/whisperCli").toString(); }
void Settings::setWhisperCliPath(const QString &p) { m_settings->setValue("tools/whisperCli", p); }
QString Settings::pythonPath() const { return m_settings->value("tools/python").toString(); }
void Settings::setPythonPath(const QString &p) { m_settings->setValue("tools/python", p); }
QString Settings::whisperModelPath() const { return m_settings->value("asr/modelPath").toString(); }
void Settings::setWhisperModelPath(const QString &p) { m_settings->setValue("asr/modelPath", p); }
QString Settings::mecabPath() const { return m_settings->value("tools/mecab").toString(); }
void Settings::setMecabPath(const QString &p) { m_settings->setValue("tools/mecab", p); }

QString Settings::asrEngine() const { return m_settings->value("asr/engine", QStringLiteral("whisper.cpp")).toString(); }
void Settings::setAsrEngine(const QString &e) { m_settings->setValue("asr/engine", e); }

// M11：ASR 语言默认跟随学习语言（本机学习语言为日语）；用户可改为手动指定。
// 注意 ini 中已有的 asr/language 值不被覆盖，只是「跟随」开启时不使用它。
bool Settings::asrLangFollowsStudy() const { return m_settings->value("asr/followStudyLang", true).toBool(); }
void Settings::setAsrLangFollowsStudy(bool v) { m_settings->setValue("asr/followStudyLang", v); }
QString Settings::asrLanguage() const
{
    if (asrLangFollowsStudy())
        return studyLanguage();
    return asrLanguageManual();
}
void Settings::setAsrLanguage(const QString &lang)
{
    m_settings->setValue("asr/language", lang);
    m_settings->setValue("asr/followStudyLang", false); // 显式指定即关闭跟随
}
QString Settings::asrLanguageManual() const
{
    return m_settings->value("asr/language", studyLanguage()).toString();
}

SegmentParams Settings::segmentParams() const
{
    SegmentParams p;
    p.noiseThreshold = m_settings->value("segment/noiseThreshold", p.noiseThreshold).toDouble();
    p.minGapMs = m_settings->value("segment/minGapMs", p.minGapMs).toInt();
    p.minSentenceMs = m_settings->value("segment/minSentenceMs", p.minSentenceMs).toInt();
    p.allowedNoiseMs = m_settings->value("segment/allowedNoiseMs", p.allowedNoiseMs).toInt();
    p.removeSilence = m_settings->value("segment/removeSilence", p.removeSilence).toBool();
    return p;
}

void Settings::setSegmentParams(const SegmentParams &p)
{
    m_settings->setValue("segment/noiseThreshold", p.noiseThreshold);
    m_settings->setValue("segment/minGapMs", p.minGapMs);
    m_settings->setValue("segment/minSentenceMs", p.minSentenceMs);
    m_settings->setValue("segment/allowedNoiseMs", p.allowedNoiseMs);
    m_settings->setValue("segment/removeSilence", p.removeSilence);
}

int Settings::repeatCount() const { return m_settings->value("study/repeatCount", 1).toInt(); }
void Settings::setRepeatCount(int n) { m_settings->setValue("study/repeatCount", qMax(1, n)); }
int Settings::repeatGapMs() const { return m_settings->value("study/repeatGapMs", 500).toInt(); }
void Settings::setRepeatGapMs(int ms) { m_settings->setValue("study/repeatGapMs", qMax(0, ms)); }
bool Settings::dictationIgnorePunctuation() const { return m_settings->value("study/dictIgnorePunct", true).toBool(); }
void Settings::setDictationIgnorePunctuation(bool v) { m_settings->setValue("study/dictIgnorePunct", v); }
bool Settings::dictationIgnoreCase() const { return m_settings->value("study/dictIgnoreCase", true).toBool(); }
void Settings::setDictationIgnoreCase(bool v) { m_settings->setValue("study/dictIgnoreCase", v); }
// M11：学习语言默认日语（用户实际学习语言）；源语言与 ASR 语言默认跟随它，
// 但 ini 中已有的 lang/source、asr/language 值不会被覆盖（只作为「不回退」的默认值）。
QString Settings::studyLanguage() const { return m_settings->value("lang/study", QStringLiteral("ja")).toString(); }
void Settings::setStudyLanguage(const QString &l) { m_settings->setValue("lang/study", l); }
QString Settings::sourceLang() const { return m_settings->value("lang/source", studyLanguage()).toString(); }
void Settings::setSourceLang(const QString &l) { m_settings->setValue("lang/source", l); }
QString Settings::targetLang() const { return m_settings->value("lang/target", QStringLiteral("zh")).toString(); }
void Settings::setTargetLang(const QString &l) { m_settings->setValue("lang/target", l); }

int Settings::dictHoverDelayMs() const { return m_settings->value("dict/hoverDelayMs", 450).toInt(); }
void Settings::setDictHoverDelayMs(int ms) { m_settings->setValue("dict/hoverDelayMs", qBound(0, ms, 5000)); }
bool Settings::dictHoverLookupEnabled() const { return m_settings->value("dict/hoverLookup", true).toBool(); }
void Settings::setDictHoverLookupEnabled(bool v) { m_settings->setValue("dict/hoverLookup", v); }

// M12：段落复述（段落范围句数 / 是否录音；默认 3 句、录音开）
int Settings::retellParagraphSize() const
{
    return qBound(1, m_settings->value("study/retellParagraphSize", 3).toInt(), 20);
}
void Settings::setRetellParagraphSize(int n)
{
    m_settings->setValue("study/retellParagraphSize", qBound(1, n, 20));
}
bool Settings::retellRecordEnabled() const
{
    return m_settings->value("study/retellRecord", true).toBool();
}
void Settings::setRetellRecordEnabled(bool v) { m_settings->setValue("study/retellRecord", v); }

QKeySequence Settings::keySequence(const QString &actionId) const
{
    const QString v = m_settings->value("keys/" + actionId).toString();
    if (!v.isEmpty())
        return QKeySequence::fromString(v, QKeySequence::PortableText);
    return defaultKeyBindings().value(actionId);
}

void Settings::setKeySequence(const QString &actionId, const QKeySequence &seq)
{
    m_settings->setValue("keys/" + actionId,
                         seq.toString(QKeySequence::PortableText));
}

QMap<QString, QKeySequence> Settings::allKeyBindings() const
{
    QMap<QString, QKeySequence> out = defaultKeyBindings();
    const auto keys = m_settings->allKeys();
    for (const QString &k : keys) {
        if (k.startsWith(QLatin1String("keys/"))) {
            const QString id = k.mid(5);
            const QString v = m_settings->value(k).toString();
            if (!v.isEmpty())
                out[id] = QKeySequence::fromString(v, QKeySequence::PortableText);
        }
    }
    return out;
}

void Settings::resetKeyBindings()
{
    const auto keys = m_settings->allKeys();
    for (const QString &k : keys) {
        if (k.startsWith(QLatin1String("keys/")))
            m_settings->remove(k);
    }
}

QMap<QString, QKeySequence> Settings::defaultKeyBindings()
{
    QMap<QString, QKeySequence> m;
    m[QStringLiteral("play.pause")] = QKeySequence(QStringLiteral("Space"));
    m[QStringLiteral("play.stop")] = QKeySequence(QStringLiteral("S"));
    m[QStringLiteral("play.forward5")] = QKeySequence(QStringLiteral("L"));
    m[QStringLiteral("play.back5")] = QKeySequence(QStringLiteral("J"));
    m[QStringLiteral("play.speedUp")] = QKeySequence(QStringLiteral("Alt+Up"));
    m[QStringLiteral("play.speedDown")] = QKeySequence(QStringLiteral("Alt+Down"));
    m[QStringLiteral("play.volumeUp")] = QKeySequence(QStringLiteral("Ctrl+Up"));
    m[QStringLiteral("play.volumeDown")] = QKeySequence(QStringLiteral("Ctrl+Down"));
    m[QStringLiteral("study.nextSentence")] = QKeySequence(QStringLiteral("N"));
    m[QStringLiteral("study.prevSentence")] = QKeySequence(QStringLiteral("P"));
    m[QStringLiteral("study.repeatSentence")] = QKeySequence(QStringLiteral("R"));
    m[QStringLiteral("study.abLoop")] = QKeySequence(QStringLiteral("B"));
    m[QStringLiteral("study.shadowing")] = QKeySequence(QStringLiteral("F"));
    m[QStringLiteral("study.dictation")] = QKeySequence(QStringLiteral("D"));
    m[QStringLiteral("study.sentenceBuild")] = QKeySequence(QStringLiteral("Ctrl+G"));
    m[QStringLiteral("study.flashcard")] = QKeySequence(QStringLiteral("Ctrl+F"));
    m[QStringLiteral("study.retell")] = QKeySequence(QStringLiteral("Ctrl+R"));
    m[QStringLiteral("study.lookupWord")] = QKeySequence(QStringLiteral("Q"));
    m[QStringLiteral("study.soundSearch")] = QKeySequence(QStringLiteral("Ctrl+Y"));
    m[QStringLiteral("bookmark.add")] = QKeySequence(QStringLiteral("Ctrl+B"));
    m[QStringLiteral("bookmark.prev")] = QKeySequence(QStringLiteral("["));
    m[QStringLiteral("bookmark.next")] = QKeySequence(QStringLiteral("]"));
    m[QStringLiteral("view.toggleTranscript")] = QKeySequence(QStringLiteral("T"));
    m[QStringLiteral("media.openFile")] = QKeySequence(QStringLiteral("Ctrl+O"));
    m[QStringLiteral("media.openUrl")] = QKeySequence(QStringLiteral("Ctrl+U"));
    m[QStringLiteral("media.generateSubs")] = QKeySequence(QStringLiteral("Ctrl+E"));
    return m;
}

QString Settings::actionDisplayName(const QString &actionId)
{
    static const QHash<QString, QString> names = {
        {QStringLiteral("play.pause"), QStringLiteral("播放/暂停")},
        {QStringLiteral("play.stop"), QStringLiteral("停止")},
        {QStringLiteral("play.forward5"), QStringLiteral("快进 5 秒")},
        {QStringLiteral("play.back5"), QStringLiteral("后退 5 秒")},
        {QStringLiteral("play.speedUp"), QStringLiteral("加速")},
        {QStringLiteral("play.speedDown"), QStringLiteral("减速")},
        {QStringLiteral("play.volumeUp"), QStringLiteral("音量+")},
        {QStringLiteral("play.volumeDown"), QStringLiteral("音量-")},
        {QStringLiteral("study.nextSentence"), QStringLiteral("下一句")},
        {QStringLiteral("study.prevSentence"), QStringLiteral("上一句")},
        {QStringLiteral("study.repeatSentence"), QStringLiteral("复读本句")},
        {QStringLiteral("study.abLoop"), QStringLiteral("A-B 区间复读")},
        {QStringLiteral("study.shadowing"), QStringLiteral("随意读(跟读)")},
        {QStringLiteral("study.dictation"), QStringLiteral("听写")},
        {QStringLiteral("study.sentenceBuild"), QStringLiteral("造句练习")},
        {QStringLiteral("study.flashcard"), QStringLiteral("原句闪卡")},
        {QStringLiteral("study.retell"), QStringLiteral("段落复述")},
        {QStringLiteral("study.lookupWord"), QStringLiteral("查询当前词")},
        {QStringLiteral("study.soundSearch"), QStringLiteral("听音查字")},
        {QStringLiteral("bookmark.add"), QStringLiteral("新增书签")},
        {QStringLiteral("bookmark.prev"), QStringLiteral("上一书签")},
        {QStringLiteral("bookmark.next"), QStringLiteral("下一书签")},
        {QStringLiteral("view.toggleTranscript"), QStringLiteral("显示/隐藏字幕侧栏")},
        {QStringLiteral("media.openFile"), QStringLiteral("打开文件")},
        {QStringLiteral("media.openUrl"), QStringLiteral("打开在线视频")},
        {QStringLiteral("media.generateSubs"), QStringLiteral("ASR 生成字幕")},
    };
    return names.value(actionId, actionId);
}

} // namespace adoloop
