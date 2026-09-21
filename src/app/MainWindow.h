#pragma once

#include <QMainWindow>

class QLabel;
class QListWidget;
class QMenu;
class QStackedWidget;
class QTabWidget;

namespace adoloop {

class WaveformWidget;
class PlayerControls;
class RepeatControls;
class TranscriptPanel;
class DictationWidget;
class SentenceBuilderWidget;
class SubtitleOverlay;
class DanmakuWidget;
class RetellWidget;

// 主窗口：左侧（播放列表/书签/生词本/闪卡），中央（视频区+字幕+弹幕+波形+控制条），
// 右侧（字幕侧栏 + 学习面板）。子控件经 getter 暴露给 AppController 装配。
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    // 中央视频/音频区（字幕与弹幕浮层叠放其上的宿主）
    QWidget *mediaHost() const { return m_mediaHost; }
    SubtitleOverlay *subtitleOverlay() const { return m_subtitleOverlay; }
    DanmakuWidget *danmakuWidget() const { return m_danmakuWidget; }
    WaveformWidget *waveformWidget() const { return m_waveform; }
    PlayerControls *playerControls() const { return m_controls; }
    RepeatControls *repeatControls() const { return m_repeatControls; }
    TranscriptPanel *transcriptPanel() const { return m_transcript; }
    DictationWidget *dictationWidget() const { return m_dictation; }
    SentenceBuilderWidget *sentenceBuilderWidget() const { return m_sentenceBuilder; }
    RetellWidget *retellWidget() const { return m_retell; }
    QTabWidget *rightTabs() const { return m_rightTabs; }
    QTabWidget *leftTabs() const { return m_leftTabs; }
    QListWidget *playlistWidget() const { return m_playlist; }
    QListWidget *bookmarkList() const { return m_bookmarkList; }
    QListWidget *vocabularyList() const { return m_vocabularyList; }
    QListWidget *flashcardList() const { return m_flashcardList; }
    QLabel *statusLabel() const { return m_status; }

    QMenu *fileMenu() const { return m_fileMenu; }
    QMenu *studyMenu() const { return m_studyMenu; }
    QMenu *toolsMenu() const { return m_toolsMenu; }
    QMenu *settingsMenu() const { return m_settingsMenu; }

    // 更新播放列表（标题 + 悬浮提示，currentRow 高亮当前媒体，-1 = 无）与书签列表
    void refreshPlaylistList(const QStringList &titles, const QStringList &tooltips, int currentRow);
    void refreshBookmarkList(const QStringList &descriptions, const QStringList &tooltips);

    // 更新生词本/闪卡列表（AppController 数据变化时调用）
    // items 为已排版好的整行文本（M12：日语生词「漢字 [かな] ② 名词 — 上下文」）
    void refreshVocabularyList(const QStringList &items, const QStringList &tooltips);
    void refreshFlashcardList(const QStringList &descriptions);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void buildMenus();

    QWidget *m_mediaHost = nullptr;
    SubtitleOverlay *m_subtitleOverlay = nullptr;
    DanmakuWidget *m_danmakuWidget = nullptr;
    WaveformWidget *m_waveform = nullptr;
    PlayerControls *m_controls = nullptr;
    RepeatControls *m_repeatControls = nullptr;
    TranscriptPanel *m_transcript = nullptr;
    DictationWidget *m_dictation = nullptr;
    SentenceBuilderWidget *m_sentenceBuilder = nullptr;
    RetellWidget *m_retell = nullptr;
    QListWidget *m_playlist = nullptr;
    QListWidget *m_bookmarkList = nullptr;
    QListWidget *m_vocabularyList = nullptr;
    QListWidget *m_flashcardList = nullptr;
    QLabel *m_status = nullptr;
    QStackedWidget *m_studyStack = nullptr;
    QTabWidget *m_leftTabs = nullptr;
    QTabWidget *m_rightTabs = nullptr;

    QMenu *m_fileMenu = nullptr;
    QMenu *m_studyMenu = nullptr;
    QMenu *m_toolsMenu = nullptr;
    QMenu *m_settingsMenu = nullptr;
};

} // namespace adoloop
