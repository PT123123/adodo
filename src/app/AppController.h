#pragma once

#include "../core/Types.h"

#include <QObject>
#include <QPointer>
#include <QHash>
#include <QPoint>
#include <QTimer>
#include <QVector>

#include <functional>

class QAction;
class QListWidget;
class QLabel;
class QMenu;
class QStackedWidget;

namespace adoloop {

class MainWindow;
class MediaEngine;
class Waveform;
class SubtitleModel;
class AsrTaskManager;
class YtDlpRunner;
class HttpClient;
class TranslationProvider;
class DictionaryProvider;
class RepeatSession;
class ShadowingSession;
class DictationSession;
class RetellSession;
class FlashcardModel;
class VocabularyModel;
class DanmakuController;
class AudioRecorder;
class DictionaryPopup;
class PlaylistModel;
class BookmarkModel;
class ResumeStore;
class PronunciationPlayer;
class SoundSearchDialog;
class ReadingStore;

// 应用控制器：装配全部模块、注册快捷键动作、串联信号、驱动学习会话。
class AppController : public QObject {
    Q_OBJECT
public:
    explicit AppController(MainWindow *window, QObject *parent = nullptr);
    ~AppController() override;

    void initialize();

    // 动作执行（快捷键/菜单共用入口）
public slots:
    void openFileDialog();
    void openUrlDialog();
    void generateSubtitlesAsr();
    void playPause();
    void stopPlayback();
    void seekForward();
    void seekBack();
    void seekRelative(Ms deltaMs);
    void speedUp();
    void speedDown();
    void volumeUp();
    void volumeDown();
    void nextSentence();
    void prevSentence();
    void repeatCurrentSentence();
    void startRepeatAt(int index);
    void toggleAbLoop();
    void startShadowing();
    void startDictation();
    void startDictationAt(int index);
    void startSentenceBuild();
    void startFlashcards();
    void startRetell(); // 段落复述（M12：当前句/意群/段落/全文）
    void lookupCurrentWord();
    void openSoundSearch(); // 听音查字（M11）
    void toggleTranscript();
    void openSettings();
    // 书签（M10）
    void addBookmark();
    void jumpToPrevBookmark();
    void jumpToNextBookmark();

private:
    void registerActions();
    void loadMedia(const QString &path, bool isOnline, const QString &streamUrl = QString());
    void loadSubtitles(const QString &subPath);
    void ensureSentences(); // 无字幕时 ASR 或占位
    void maybeTranslateSubtitles(); // 批量补齐译文（ASR/无译文时）
    // M11：语言注入与分词升级
    Lang studyLang() const;
    void applyLanguageSettings();  // 把学习语言铺到字幕/词典/学习模块
    void applyOverlaySettings();   // 悬停延时/仅点击查词（字幕浮层）
    void upgradeSentenceTokens();  // 配置了 MeCab/Python 桥时批量补分词（缺失/失败自动跳过）
    void playRange(Ms start, Ms end);
    void updatePosition(Ms pos);
    void onSentenceActivated(int index);
    void lookupWord(const QString &word, int sentenceIndex, const QPoint &globalPos = QPoint());
    void addToVocabulary(const QString &word, const QString &sentence, const QString &source);
    void addToSentenceBook(int index);
    void saveAll();
    void extractSentenceAudio(const Sentence &s, std::function<void(const QVector<float> &, int)> cb);
    void extractRangeAudio(Ms start, Ms end, std::function<void(const QVector<float> &, int)> cb);
    void saveSentenceBook();

    // 日语化读音（M12）：生词本/闪卡列表按读音重排显示
    void refreshVocabularyUi();
    void refreshFlashcardUi();

    // 段落复述（M12）
    void applyRetellSettings();
    void refreshRetellUi();
    void beginRetellCapture();
    void stopRetellCapture(bool finalize);

    // 播放列表（M10）
    QString mediaKey() const; // 当前媒体的持久化键（本地=规范路径；在线=标题）
    void openPlaylistRow(int row);
    void removePlaylistRow(int row);
    void clearPlaylist();
    void refreshPlaylistUi();

    // 书签（M10）
    void refreshBookmarkUi();
    void rebuildBookmarkMenu();
    void jumpToBookmarkRow(int row);
    void clearBookmarksForCurrentMedia();

    // 断点续播（M10）
    void saveResumeForCurrent(bool force);
    void restoreResumeIfPending();

    // 影子跟读管线
    void beginShadowingCapture();
    void stopShadowingCapture(bool finalize);
    void onVadTick();
    void onShadowingResult(int index, const ShadowingScore &score, const QString &recPath);

    // 听音查字（M11）
    void startSoundSearchRecording();
    void stopSoundSearchRecording();

    MainWindow *m_win = nullptr;

    MediaEngine *m_engine = nullptr;
    Waveform *m_waveform = nullptr;
    SubtitleModel *m_subtitles = nullptr;
    AsrTaskManager *m_asr = nullptr;
    YtDlpRunner *m_ytdlp = nullptr;
    HttpClient *m_http = nullptr;
    TranslationProvider *m_translator = nullptr;
    DictionaryProvider *m_dictionary = nullptr;
    RepeatSession *m_repeat = nullptr;
    ShadowingSession *m_shadowing = nullptr;
    DictationSession *m_dictation = nullptr;
    RetellSession *m_retell = nullptr;      // 段落复述（M12）
    FlashcardModel *m_flashcards = nullptr;
    VocabularyModel *m_vocabulary = nullptr;
    DanmakuController *m_danmaku = nullptr;
    ReadingStore *m_readings = nullptr;     // 读音缓存（M12：查词成功写入，离线复用）
    AudioRecorder *m_recorder = nullptr;
    DictionaryPopup *m_dictPopup = nullptr;
    PronunciationPlayer *m_pronunciation = nullptr; // 词典发音（独立播放器，不干扰 mpv）
    SoundSearchDialog *m_soundDialog = nullptr;     // 听音查字（懒创建）
    quint64 m_soundSearchTaskId = 0;                // 听音查字的 ASR 任务（与字幕 ASR 区分）
    QString m_soundSearchWav;
    PlaylistModel *m_playlist = nullptr;
    BookmarkModel *m_bookmarks = nullptr;
    ResumeStore *m_resume = nullptr;

    QString m_currentMedia;
    QString m_currentTitle;
    QString m_currentStreamUrl;
    bool m_isOnline = false;
    int m_currentSentence = -1;
    Ms m_activeRangeEnd = 0; // 临时播放区间终点（非复读会话）
    Ms m_abA = -1;
    Ms m_abB = -1;
    quint64 m_lookupToken = 0;
    QPoint m_lookupPos;
    QString m_lookupWordText;
    // 字幕悬停取词（M11）：浮层延时触发 → 控制器去抖后查词
    QTimer *m_hoverDebounce = nullptr;
    QString m_hoverWord;
    int m_hoverSentence = -1;
    QHash<quint64, int> m_pendingTrans; // 翻译 token → 句索引
    QStringList m_sentenceBook;

    // 跟读管线状态
    QTimer *m_vadTimer = nullptr;
    QByteArray m_vadAccum;
    QString m_pendingRecordingPath;
    bool m_shadowingActive = false;
    bool m_retellActive = false; // 复述录音（缓冲模式）中

    // 书签菜单（动态项每次书签变化后重建）
    QMenu *m_bookmarkMenu = nullptr;
    QAction *m_bookmarkDynSep = nullptr;

    // 断点续播状态
    QString m_resumeKey;            // 当前媒体的续播键
    Ms m_pendingResumeMs = 0;       // 待恢复位置（0 = 不恢复）
    Ms m_lastResumePos = -1;        // 上次记录的播放位置（落点节流）
    bool m_resumeSuspended = false; // 「停止」归零后暂停记录，避免覆盖续播点

    // 数据文件路径
    QString m_sentenceBookPath;
    QString m_vocabPath;
    QString m_flashcardPath;
    QString m_playlistPath;
    QString m_bookmarkPath;
    QString m_resumePath;
    QString m_readingsPath; // readings.json（M12 读音缓存）
    QString m_retellPath;   // retell.json（M12 复述练习历史）
};

} // namespace adoloop
