#include "AppController.h"
#include "MainWindow.h"
#include "Settings.h"

#include "../asr/AsrTaskManager.h"
#include "../asr/AsrEngine.h"
#include "../core/AudioAnalysis.h"
#include "../core/AudioRecorder.h"
#include "../core/BookmarkModel.h"
#include "../core/MediaEngine.h"
#include "../core/PlaylistModel.h"
#include "../core/PronunciationPlayer.h"
#include "../core/ReadingStore.h"
#include "../core/ResumeStore.h"
#include "../core/SubtitleModel.h"
#include "../core/Tokenizer.h"
#include "../core/Waveform.h"
#include "../net/DictionaryParser.h"
#include "../net/DictionaryProvider.h"
#include "../net/HttpClient.h"
#include "../net/YtDlpRunner.h"
#include "../study/DanmakuController.h"
#include "../study/DictationSession.h"
#include "../study/FlashcardModel.h"
#include "../study/RepeatSession.h"
#include "../study/RetellSession.h"
#include "../study/ShadowingSession.h"
#include "../study/VocabularyModel.h"
#include "../subtitle/TranslationProvider.h"
#include "../ui/DanmakuWidget.h"
#include "../ui/DictationWidget.h"
#include "../ui/DictionaryPopup.h"
#include "../ui/PlayerControls.h"
#include "../ui/RepeatControls.h"
#include "../ui/RetellWidget.h"
#include "../ui/SentenceBuilderWidget.h"
#include "../ui/SettingsDialog.h"
#include "../ui/SoundSearchDialog.h"
#include "../ui/SubtitleOverlay.h"
#include "../ui/TranscriptPanel.h"
#include "../ui/WaveformWidget.h"
#include "../util/StringUtil.h"
#include "../util/Subprocess.h"
#include "../util/TimeUtil.h"

#include <QAction>
#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QTabWidget>
#include <QUrl>

namespace adoloop {

namespace {

// 断点续播落点间隔：播放中每前进 5 秒记一次（并立即落盘）
constexpr Ms kResumeCheckpointMs = 5000;

QVector<float> s16leToFloat(const QByteArray &raw)
{
    QVector<float> out;
    const int n = raw.size() / 2;
    out.reserve(n);
    for (int i = 0; i < n; ++i) {
        qint16 s;
        memcpy(&s, raw.constData() + i * 2, 2);
        out.append(float(s) / 32768.0f);
    }
    return out;
}

// 写合法 WAV（16kHz / 单声道 / 16bit）：跟读与段落复述录音共用（M12 抽出）
bool writeWav16kMono(const QString &path, const QByteArray &pcm)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    const quint32 dataLen = quint32(pcm.size());
    const quint32 byteRate = 16000u * 1u * 16u / 8u;
    QByteArray h(44, '\0');
    auto put32 = [&](qint64 off, quint32 v) {
        h[off] = char(v & 0xFF);
        h[off + 1] = char((v >> 8) & 0xFF);
        h[off + 2] = char((v >> 16) & 0xFF);
        h[off + 3] = char((v >> 24) & 0xFF);
    };
    memcpy(h.data(), "RIFF", 4);
    put32(4, 36 + dataLen);
    memcpy(h.data() + 8, "WAVE", 4);
    memcpy(h.data() + 12, "fmt ", 4);
    put32(16, 16);
    h[20] = 1;
    h[21] = 0;
    h[22] = 1;
    h[23] = 0;
    put32(24, 16000);
    put32(28, byteRate);
    h[32] = 2;
    h[33] = 0;
    h[34] = 16;
    h[35] = 0;
    memcpy(h.data() + 36, "data", 4);
    put32(40, dataLen);
    f.write(h);
    f.write(pcm);
    f.close();
    return true;
}

} // namespace

AppController::AppController(MainWindow *window, QObject *parent)
    : QObject(parent)
    , m_win(window)
{
    Settings &s = Settings::instance();
    m_sentenceBookPath = QDir(s.dataDir()).filePath(QStringLiteral("sentencebook.json"));
    m_vocabPath = QDir(s.dataDir()).filePath(QStringLiteral("vocabulary.json"));
    m_flashcardPath = QDir(s.dataDir()).filePath(QStringLiteral("flashcards.json"));
    m_playlistPath = QDir(s.dataDir()).filePath(QStringLiteral("playlist.json"));
    m_bookmarkPath = QDir(s.dataDir()).filePath(QStringLiteral("bookmarks.json"));
    m_resumePath = QDir(s.dataDir()).filePath(QStringLiteral("resume.json"));
    m_readingsPath = QDir(s.dataDir()).filePath(QStringLiteral("readings.json"));
    m_retellPath = QDir(s.dataDir()).filePath(QStringLiteral("retell.json"));
}

AppController::~AppController()
{
    saveResumeForCurrent(true); // 退出前落最后一个续播点
    saveAll();
}

void AppController::initialize()
{
    Settings &s = Settings::instance();

    m_engine = MediaEngine::create(this);
    m_waveform = new Waveform(this);
    m_subtitles = new SubtitleModel(this);
    m_asr = new AsrTaskManager(this);
    m_ytdlp = new YtDlpRunner(this);
    m_http = new HttpClient(this);
    m_translator = new TranslationProvider(m_http, this);
    m_dictionary = new DictionaryProvider(m_http, this);
    m_repeat = new RepeatSession(this);
    m_shadowing = new ShadowingSession(this);
    m_dictation = new DictationSession(this);
    m_retell = new RetellSession(this);
    m_flashcards = new FlashcardModel(this);
    m_vocabulary = new VocabularyModel(this);
    m_danmaku = new DanmakuController(this);
    m_readings = new ReadingStore(this); // M12：查词得到的读音落盘，离线复用
    m_recorder = new AudioRecorder(this);
    connect(m_recorder, &AudioRecorder::failed, this, [this](const QString &e) {
        m_win->statusLabel()->setText(QStringLiteral("录音失败：%1").arg(e));
        if (m_soundDialog)
            m_soundDialog->setStatus(QStringLiteral("录音失败：%1").arg(e));
    });
    m_dictPopup = new DictionaryPopup(m_win);
    m_pronunciation = new PronunciationPlayer(this);
    m_pronunciation->setVolume(Settings::instance().volume());
    m_playlist = new PlaylistModel(this);
    m_bookmarks = new BookmarkModel(this);
    m_resume = new ResumeStore(this);

    m_vadTimer = new QTimer(this);
    m_vadTimer->setInterval(100);
    connect(m_vadTimer, &QTimer::timeout, this, &AppController::onVadTick);

    // M11：学习语言注入（分词/词典解析/发音都按它走；core 层不反向依赖 Settings）
    applyLanguageSettings();

    // 持久化加载
    m_vocabulary->loadFrom(m_vocabPath);
    m_flashcards->loadFrom(m_flashcardPath);
    m_playlist->loadFrom(m_playlistPath);
    m_bookmarks->loadFrom(m_bookmarkPath);
    m_resume->loadFrom(m_resumePath);
    m_readings->loadFrom(m_readingsPath);
    m_retell->loadHistoryFrom(m_retellPath);

    // M12：读音缓存是生词本/闪卡的读音来源（旧数据缺读音时也会被补齐）
    m_vocabulary->setReadingStore(m_readings);
    m_flashcards->setReadingStore(m_readings);
    m_win->subtitleOverlay()->setReadingStore(m_readings);
    connect(m_readings, &ReadingStore::changed, this, [this]() {
        m_readings->saveTo(m_readingsPath);
        // 新查到的读音立刻反映到列表/弹幕（旧条目缺读音的也一并补上）
        m_vocabulary->fillMissingReadings();
        refreshVocabularyUi();
        refreshFlashcardUi();
    });

    // 播放列表 / 书签 / 断点续播（M10）
    connect(m_playlist, &PlaylistModel::changed, this, [this]() {
        refreshPlaylistUi();
        m_playlist->saveTo(m_playlistPath);
    });
    connect(m_bookmarks, &BookmarkModel::changed, this, [this]() {
        refreshBookmarkUi();
        rebuildBookmarkMenu();
        m_bookmarks->saveTo(m_bookmarkPath);
    });
    connect(m_resume, &ResumeStore::changed, this,
            [this]() { m_resume->saveTo(m_resumePath); });

    // 数据绑定
    m_repeat->setSentences(&m_subtitles->sentences());
    m_shadowing->setSentences(&m_subtitles->sentences());
    m_danmaku->setVocabulary(m_vocabulary);
    m_win->transcriptPanel()->setSubtitleModel(m_subtitles);
    m_win->waveformWidget()->setSentences(&m_subtitles->sentences());
    connect(m_subtitles, &SubtitleModel::changed, this, [this]() {
        m_win->waveformWidget()->update();
        m_win->subtitleOverlay()->update();
    });

    // 播放引擎 → UI
    connect(m_engine, &MediaEngine::positionChanged, this, &AppController::updatePosition);
    connect(m_engine, &MediaEngine::durationChanged, this, [this](Ms d) {
        m_win->playerControls()->setDuration(d);
        if (!m_isOnline && !m_currentMedia.isEmpty())
            m_playlist->setDuration(m_currentMedia, d); // 列表悬浮提示显示时长
    });
    connect(m_engine, &MediaEngine::playStateChanged, this, [this](bool p) {
        m_win->playerControls()->setPlaying(p);
        if (!p)
            saveResumeForCurrent(true); // 暂停即落点
    });
    connect(m_engine, &MediaEngine::loadedChanged, this, [this](bool l) {
        m_win->playerControls()->setLoaded(l);
        if (l)
            restoreResumeIfPending();
    });
    connect(m_engine, &MediaEngine::eofReached, this, [this]() {
        m_repeat->onEof();
        m_retell->onModelFinished(); // 段末（引擎 eof）也推进复述状态
        m_resume->forget(m_resumeKey); // 整段听完：清掉续播点，下次从头
        m_win->statusLabel()->setText(QStringLiteral("播放结束"));
    });
    connect(m_engine, &MediaEngine::errorOccurred, this,
            [this](const QString &e) { m_win->statusLabel()->setText(e); });

    // 波形
    connect(m_waveform, &Waveform::ready, this,
            [this]() { m_win->waveformWidget()->setWaveform(m_waveform); });
    connect(m_waveform, &Waveform::failed, this,
            [this](const QString &e) { m_win->statusLabel()->setText(e); });

    // 播放控制条
    auto *controls = m_win->playerControls();
    connect(controls, &PlayerControls::playPauseClicked, this, &AppController::playPause);
    connect(controls, &PlayerControls::stopClicked, this, &AppController::stopPlayback);
    connect(controls, &PlayerControls::seekRequested, this,
            [this](Ms pos) { m_engine->seek(pos); });
    connect(controls, &PlayerControls::rateChanged, this,
            [this](double r) {
                m_engine->setRate(r);
                Settings::instance().setDefaultRate(r);
            });
    connect(controls, &PlayerControls::volumeChanged, this,
            [this](int v) {
                m_engine->setVolume(v);
                Settings::instance().setVolume(v);
            });
    connect(controls, &PlayerControls::abSetA, this, [this]() {
        m_abA = m_engine->position();
        m_win->playerControls()->setAbMarkers(m_abA, m_abB);
        m_win->statusLabel()->setText(QStringLiteral("A 点已标记 @ %1").arg(m_abA));
    });
    connect(controls, &PlayerControls::abSetB, this, [this]() {
        m_abB = m_engine->position();
        m_win->playerControls()->setAbMarkers(m_abA, m_abB);
        m_win->statusLabel()->setText(QStringLiteral("B 点已标记 @ %1").arg(m_abB));
    });
    connect(controls, &PlayerControls::abLoopToggled, this, &AppController::toggleAbLoop);

    // 复读
    auto *rep = m_win->repeatControls();
    connect(rep, &RepeatControls::prevSentenceRequested, this, &AppController::prevSentence);
    connect(rep, &RepeatControls::nextSentenceRequested, this, &AppController::nextSentence);
    connect(rep, &RepeatControls::repeatSentenceRequested, this, &AppController::repeatCurrentSentence);
    connect(rep, &RepeatControls::fullScriptToggled, this, [this](bool on) {
        if (on && m_subtitles->count() > 0) {
            m_repeat->setConfig({Settings::instance().repeatCount(),
                                 Settings::instance().repeatGapMs(), true});
            m_repeat->startFullScript(qMax(0, m_currentSentence));
        } else {
            m_repeat->stop();
        }
    });
    connect(rep, &RepeatControls::repeatConfigChanged, this,
            [](int count, int gap) {
                Settings::instance().setRepeatCount(count);
                Settings::instance().setRepeatGapMs(gap);
            });
    connect(m_repeat, &RepeatSession::commandPlayRange, this, &AppController::playRange);
    connect(m_repeat, &RepeatSession::commandPause, this,
            [this]() { m_engine->pause(); });
    connect(m_repeat, &RepeatSession::sentenceChanged, this,
            [this](int idx) { onSentenceActivated(idx); });

    // 字幕侧栏
    auto *tp = m_win->transcriptPanel();
    connect(tp, &TranscriptPanel::sentenceActivated, this, &AppController::onSentenceActivated);
    connect(tp, &TranscriptPanel::repeatRequested, this,
            [this](int idx) { startRepeatAt(idx); });
    connect(tp, &TranscriptPanel::dictationRequested, this,
            [this](int idx) { startDictationAt(idx); });
    connect(tp, &TranscriptPanel::lookupWordRequested, this,
            [this](const QString &w, int idx) { lookupWord(w, idx); });
    connect(tp, &TranscriptPanel::addToSentenceBook, this,
            [this](int idx) { addToSentenceBook(idx); });
    connect(tp, &TranscriptPanel::addAllWordsToVocabulary, this,
            [this](int idx) {
                if (idx < 0 || idx >= m_subtitles->count())
                    return;
                const Sentence &s = m_subtitles->at(idx);
                for (const QString &w : s.wordList())
                    addToVocabulary(w, s.text, m_currentTitle);
            });

    // 字幕取词（M11）：浮层做像素级词命中，悬停延时后再由本控制器去抖查词
    // 点击 → 立即查词；点击空白 → 浮层清除高亮
    m_hoverDebounce = new QTimer(this);
    m_hoverDebounce->setSingleShot(true);
    m_hoverDebounce->setInterval(150);
    connect(m_hoverDebounce, &QTimer::timeout, this, [this]() {
        if (!m_hoverWord.isEmpty())
            lookupWord(m_hoverWord, m_hoverSentence, QCursor::pos());
    });
    auto *overlay = m_win->subtitleOverlay();
    connect(overlay, &SubtitleOverlay::wordClicked, this,
            [this](const QString &w, int idx) { lookupWord(w, idx); });
    connect(overlay, &SubtitleOverlay::wordHovered, this,
            [this](const QString &w, int idx) {
                m_hoverWord = w;
                m_hoverSentence = idx;
                if (m_hoverDebounce)
                    m_hoverDebounce->start();
            });
    connect(overlay, &SubtitleOverlay::hoverCleared, this, [this]() {
        m_hoverWord.clear();
        if (m_hoverDebounce)
            m_hoverDebounce->stop();
    });
    applyOverlaySettings();

    // 弹幕
    connect(m_danmaku, &DanmakuController::danmakuEmitted, this,
            [this](const DanmakuController::Item &it) {
                m_win->danmakuWidget()->setEnabled2(true);
                m_win->danmakuWidget()->showDanmaku(it);
            });
    connect(m_win->danmakuWidget(), &DanmakuWidget::wordClicked, this,
            [this](const QString &w) { lookupWord(w, m_currentSentence); });

    // 听写
    connect(m_dictation, &DictationSession::commandPlayRange, this, &AppController::playRange);
    connect(m_dictation, &DictationSession::commandPause, this,
            [this]() { m_engine->pause(); });
    connect(m_dictation, &DictationSession::commandRepeatWord, this,
            [this](int wordIndex) {
                if (m_currentSentence < 0 || m_currentSentence >= m_subtitles->count())
                    return;
                const Sentence &s = m_subtitles->at(m_currentSentence);
                const QVector<WordToken> ws = s.estimatedWords();
                if (wordIndex >= 0 && wordIndex < ws.size())
                    playRange(ws[wordIndex].start, qMax(ws[wordIndex].end, ws[wordIndex].start + 200));
            });
    connect(m_dictation, &DictationSession::wordChecked, this,
            [this](int idx, bool ok) {
                Q_UNUSED(idx);
                m_win->dictationWidget()->setFeedback(ok);
                m_win->dictationWidget()->refreshSlots(
                    m_dictation->slotWords(), m_dictation->slotFilled());
            });
    connect(m_dictation, &DictationSession::sentenceChecked, this,
            [this](bool ok) { m_win->dictationWidget()->setFeedback(ok); });
    connect(m_dictation, &DictationSession::reportReady, this,
            [this](const DictationSession::Report &r) {
                const QString path = QDir(Settings::instance().dictationsDir())
                                         .filePath(QStringLiteral("dictation-%1.json")
                                                       .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss"))));
                QFile f(path);
                if (f.open(QIODevice::WriteOnly))
                    f.write(r.toJson().toUtf8());
                m_win->dictationWidget()->showReport(
                    QStringLiteral("共 %1 项，正确 %2，正确率 %3%")
                        .arg(r.total)
                        .arg(r.correct)
                        .arg(int(r.accuracy * 100)));
            });

    auto *dw = m_win->dictationWidget();
    dw->attachSession(m_dictation);
    connect(m_dictation, &DictationSession::stateChanged, this,
            [dw, this]() { dw->attachSession(m_dictation); });
    connect(dw, &DictationWidget::inputSubmitted, this,
            [this](const QString &t) { m_dictation->inputText(t); });
    connect(dw, &DictationWidget::revealRequested, this,
            [this]() { m_dictation->reveal(); });
    connect(dw, &DictationWidget::nextRequested, this,
            [this]() { m_dictation->next(); });
    connect(dw, &DictationWidget::beginRequested, this,
            [this](DictationSession *) { startDictationAt(qMax(0, m_currentSentence)); });
    connect(dw, &DictationWidget::stopRequested, this,
            [this]() { m_dictation->stop(); });

    // 造句
    auto *sb = m_win->sentenceBuilderWidget();
    connect(sb, &SentenceBuilderWidget::pickRequested, sb, [sb](int i) {
        sb->refresh();
    });
    connect(sb, &SentenceBuilderWidget::unpickRequested, sb, [sb](int i) {
        sb->refresh();
    });
    connect(sb, &SentenceBuilderWidget::nextSentenceRequested, this,
            [this]() {
                const int idx = qBound(0, m_currentSentence + 1, m_subtitles->count() - 1);
                if (idx >= 0 && idx < m_subtitles->count())
                    m_win->sentenceBuilderWidget()->loadSentence(m_subtitles->at(idx).text);
            });

    // 段落复述（M12）：会话是纯逻辑状态机，命令（播放/录音）由本控制器执行
    m_retell->setSentences(&m_subtitles->sentences());
    auto *rw = m_win->retellWidget();
    rw->attachSession(m_retell);
    connect(rw, &RetellWidget::startRequested, this, &AppController::startRetell);
    connect(rw, &RetellWidget::stopRequested, this, [this]() { m_retell->stop(); });
    connect(rw, &RetellWidget::startRetellingRequested, this,
            [this]() { m_retell->onUserStartRetelling(); });
    connect(rw, &RetellWidget::stopRetellingRequested, this,
            [this]() { m_retell->onUserStopRetelling(); });
    connect(rw, &RetellWidget::revealRequested, this, [this]() { m_retell->reveal(); });
    connect(rw, &RetellWidget::nextRequested, this, [this]() { m_retell->nextSegment(); });
    connect(rw, &RetellWidget::rateRequested, this, [this](int q) { m_retell->rate(q); });
    connect(rw, &RetellWidget::optionsChanged, this, [this]() {
        Settings &s = Settings::instance();
        s.setRetellParagraphSize(m_win->retellWidget()->paragraphSize());
        s.setRetellRecordEnabled(m_win->retellWidget()->recordEnabled());
        applyRetellSettings();
    });
    connect(m_retell, &RetellSession::commandPlayRange, this, &AppController::playRange);
    connect(m_retell, &RetellSession::commandPause, this, [this]() { m_engine->pause(); });
    connect(m_retell, &RetellSession::commandStartBufferRecording, this,
            &AppController::beginRetellCapture);
    connect(m_retell, &RetellSession::commandStopBufferRecording, this,
            [this]() { stopRetellCapture(true); });
    connect(m_retell, &RetellSession::commandCancelBufferRecording, this,
            [this]() { stopRetellCapture(false); });
    connect(m_retell, &RetellSession::concealChanged, this, [this](bool on) {
        // 隐藏/揭晓落到底：字幕浮层在复述进行中不显示原文（音频照常播放）
        m_win->subtitleOverlay()->setConcealed(on);
    });
    connect(m_retell, &RetellSession::stateChanged, this, [this]() { refreshRetellUi(); });
    connect(m_retell, &RetellSession::segmentChanged, this,
            [this](int, const RetellSession::Segment &) { refreshRetellUi(); });
    connect(m_retell, &RetellSession::historyChanged, this, [this]() {
        m_retell->saveHistoryTo(m_retellPath);
        refreshRetellUi();
    });
    connect(m_retell, &RetellSession::resultReady, this, [this](const RetellSession::Item &it) {
        m_win->statusLabel()->setText(
            QStringLiteral("复述记录：第 %1–%2 句 · 自评 %3/5%4")
                .arg(it.firstSentence + 1)
                .arg(it.lastSentence + 1)
                .arg(it.quality < 0 ? 0 : it.quality)
                .arg(it.score >= 0 ? QStringLiteral(" · 包络比对 %1 分").arg(it.score)
                                   : QStringLiteral(" · 未录音")));
    });
    connect(m_retell, &RetellSession::notice, this, [this](const QString &t) {
        m_win->statusLabel()->setText(t);
        m_win->retellWidget()->setHint(t);
    });
    connect(m_retell, &RetellSession::finished, this, [this]() {
        m_win->statusLabel()->setText(
            QStringLiteral("段落复述完成（本次 %1 段，历史 %2 条）")
                .arg(m_retell->segmentCount())
                .arg(m_retell->history().size()));
    });

    // 查词弹窗
    connect(m_dictionary, &DictionaryProvider::finished, this,
            [this](quint64 tk, const DictEntry &e) {
                if (tk == m_lookupToken) {
                    // M12：查词成功 → 读音（假名/声调/词性）写入本地缓存（失败不写）
                    m_readings->putFromDict(e, studyLang());
                    m_dictPopup->showEntry(e, m_lookupPos);
                    const QString sent = (m_currentSentence >= 0 && m_currentSentence < m_subtitles->count())
                        ? m_subtitles->at(m_currentSentence).text
                        : QString();
                    m_danmaku->trigger(e.word, sent, 2, e.reading);
                }
            });
    connect(m_dictionary, &DictionaryProvider::failed, this,
            [this](quint64 tk, const QString &err) {
                if (tk == m_lookupToken)
                    m_dictPopup->showError(m_lookupWordText, err, m_lookupPos);
            });
    connect(m_dictPopup, &DictionaryPopup::addToVocabularyRequested, this,
            [this](const QString &w) {
                const QString sent = (m_currentSentence >= 0 && m_currentSentence < m_subtitles->count())
                    ? m_subtitles->at(m_currentSentence).text
                    : QString();
                addToVocabulary(w, sent, m_currentTitle);
            });
    connect(m_dictPopup, &DictionaryPopup::pronounceRequested, this,
            [this](const QString &w, const QString &audioUrl) {
                // M11：词典发音（有道 dictvoice）——独立 QMediaPlayer，不打断 mpv 主播放
                const QString url = audioUrl.isEmpty()
                    ? dictparse::audioUrlForWord(w, studyLang())
                    : audioUrl;
                if (url.isEmpty()) {
                    m_win->statusLabel()->setText(QStringLiteral("词典未提供「%1」的发音").arg(w));
                    return;
                }
                m_pronunciation->play(url);
                m_win->statusLabel()->setText(QStringLiteral("朗读：%1").arg(w));
            });
    connect(m_pronunciation, &PronunciationPlayer::failed, this,
            [this](const QString &e) {
                m_win->statusLabel()->setText(
                    QStringLiteral("发音播放失败：%1（可改用系统 TTS，或检查网络）").arg(e));
            });
    connect(m_dictPopup, &DictionaryPopup::lookupRetry, this,
            [this](const QString &w) { lookupWord(w, m_currentSentence); });

    // ASR
    connect(m_asr, &AsrTaskManager::taskProgress, this,
            [this](quint64, float f, const QString &st) {
                m_win->statusLabel()->setText(QStringLiteral("%1 %2%").arg(st).arg(int(f * 100)));
            });
    connect(m_asr, &AsrTaskManager::taskFinished, this,
            [this](quint64 id, const QVector<AsrSegment> &seg) {
                // 听音查字任务：只回填输入框并查词，不导入字幕
                if (id == m_soundSearchTaskId) {
                    m_soundSearchTaskId = 0;
                    QString text;
                    for (const AsrSegment &s : seg)
                        text += s.text;
                    text = strutil::stripPunctuation(text).remove(QLatin1Char(' '));
                    if (m_soundDialog) {
                        m_soundDialog->setBusy(false);
                        if (text.isEmpty()) {
                            m_soundDialog->setStatus(
                                QStringLiteral("没听清，请再说一次，或直接输入汉字/假名/罗马字"));
                        } else {
                            m_soundDialog->setRecognizedText(text);
                            m_soundDialog->setStatus(
                                QStringLiteral("识别结果：%1（已自动查词）").arg(text));
                        }
                    }
                    if (!text.isEmpty())
                        lookupWord(text, m_currentSentence, QCursor::pos());
                    return;
                }
                m_subtitles->importAsrSegments(seg);
                m_win->statusLabel()->setText(
                    QStringLiteral("ASR 完成：%1 句").arg(m_subtitles->count()));
                upgradeSentenceTokens();
                maybeTranslateSubtitles();
            });
    connect(m_asr, &AsrTaskManager::taskFailed, this,
            [this](quint64 id, const QString &e) {
                if (id == m_soundSearchTaskId) {
                    m_soundSearchTaskId = 0;
                    if (m_soundDialog) {
                        m_soundDialog->setBusy(false);
                        m_soundDialog->setStatus(
                            QStringLiteral("识别失败：%1\n（未安装 ASR 时可在「设置 → 通用/引擎」配置 "
                                           "whisper.cpp 或 python+faster-whisper；输入框查词不受影响）")
                                .arg(e));
                    }
                    return;
                }
                m_win->statusLabel()->setText(QStringLiteral("ASR 失败：%1").arg(e));
            });

    // yt-dlp
    connect(m_ytdlp, &YtDlpRunner::resolved, this,
            [this](const MediaInfo &info, const QString &streamUrl) {
                m_win->statusLabel()->setText(QStringLiteral("已解析：%1").arg(info.title));
                loadMedia(info.title, true, streamUrl);
            });
    connect(m_ytdlp, &YtDlpRunner::subtitlesReady, this,
            [this](const QString &path) { loadSubtitles(path); });
    connect(m_ytdlp, &YtDlpRunner::failed, this,
            [this](const QString &e) { m_win->statusLabel()->setText(e); });

    // 影子跟读
    connect(m_shadowing, &ShadowingSession::commandPlaySentence, this,
            [this](int idx) {
                if (idx >= 0 && idx < m_subtitles->count()) {
                    const Sentence &s = m_subtitles->at(idx);
                    playRange(s.start, s.end);
                }
            });
    connect(m_shadowing, &ShadowingSession::commandPause, this,
            [this]() { m_engine->pause(); });
    connect(m_shadowing, &ShadowingSession::commandStartBufferRecording, this,
            &AppController::beginShadowingCapture);
    connect(m_shadowing, &ShadowingSession::commandStopBufferRecording, this,
            [this]() { stopShadowingCapture(true); });
    connect(m_shadowing, &ShadowingSession::commandCancelBufferRecording, this,
            [this]() { stopShadowingCapture(false); });
    connect(m_shadowing, &ShadowingSession::commandAutoAdvance, this,
            [this]() {
                if (m_currentSentence + 1 < m_subtitles->count())
                    m_shadowing->begin(m_currentSentence + 1, m_shadowing->mode());
            });
    connect(m_shadowing, &ShadowingSession::resultReady, this,
            [this](const ShadowingSession::Result &r) {
                onShadowingResult(r.sentenceIndex, r.score, m_pendingRecordingPath);
                m_pendingRecordingPath.clear();
            });

    // 翻译（批量字幕译文）
    connect(m_translator, &TranslationProvider::finished, this,
            [this](quint64 token, const QString &translation) {
                if (!m_pendingTrans.contains(token))
                    return;
                const int idx = m_pendingTrans.take(token);
                if (idx >= 0 && idx < m_subtitles->count()) {
                    m_subtitles->mutableSentences()[idx].translation = translation;
                    emit m_subtitles->changed();
                }
            });
    connect(m_translator, &TranslationProvider::failed, this,
            [this](quint64 token, const QString &) {
                m_pendingTrans.remove(token);
            });

    // 生词本/闪卡列表刷新（M12：显示读音/声调/词性）
    connect(m_vocabulary, &VocabularyModel::changed, this, [this]() {
        refreshVocabularyUi();
        saveAll();
    });
    connect(m_flashcards, &FlashcardModel::changed, this, [this]() {
        refreshFlashcardUi();
        saveAll();
    });
    connect(m_flashcards, &FlashcardModel::dataChanged, this, [this]() { saveAll(); });

    // 播放列表交互（双击播放 / 右键移除·清空）
    auto *plw = m_win->playlistWidget();
    connect(plw, &QListWidget::itemDoubleClicked, this, [this, plw](QListWidgetItem *item) {
        if (item)
            openPlaylistRow(plw->row(item));
    });
    connect(plw, &QListWidget::customContextMenuRequested, this, [this, plw](const QPoint &pos) {
        QListWidgetItem *item = plw->itemAt(pos);
        QMenu menu(plw);
        QAction *play = menu.addAction(QStringLiteral("播放"));
        QAction *remove = menu.addAction(QStringLiteral("从列表移除"));
        menu.addSeparator();
        QAction *clear = menu.addAction(QStringLiteral("清空播放列表"));
        play->setEnabled(item != nullptr);
        remove->setEnabled(item != nullptr);
        clear->setEnabled(!m_playlist->entries().isEmpty());
        QAction *chosen = menu.exec(plw->viewport()->mapToGlobal(pos));
        if (!chosen)
            return;
        const int row = item ? plw->row(item) : -1;
        if (chosen == play)
            openPlaylistRow(row);
        else if (chosen == remove)
            removePlaylistRow(row);
        else if (chosen == clear)
            clearPlaylist();
    });

    // 书签交互（双击跳转 / 右键删除·清空）
    auto *bmw = m_win->bookmarkList();
    connect(bmw, &QListWidget::itemDoubleClicked, this, [this, bmw](QListWidgetItem *item) {
        if (item)
            jumpToBookmarkRow(bmw->row(item));
    });
    connect(bmw, &QListWidget::customContextMenuRequested, this, [this, bmw](const QPoint &pos) {
        QListWidgetItem *item = bmw->itemAt(pos);
        QMenu menu(bmw);
        QAction *jump = menu.addAction(QStringLiteral("跳转"));
        QAction *del = menu.addAction(QStringLiteral("删除书签"));
        menu.addSeparator();
        QAction *clear = menu.addAction(QStringLiteral("清空本文件书签"));
        jump->setEnabled(item != nullptr);
        del->setEnabled(item != nullptr);
        clear->setEnabled(m_bookmarks->rowCount() > 0);
        QAction *chosen = menu.exec(bmw->viewport()->mapToGlobal(pos));
        if (!chosen)
            return;
        const int row = item ? bmw->row(item) : -1;
        if (chosen == jump) {
            jumpToBookmarkRow(row);
        } else if (chosen == del) {
            if (m_bookmarks->removeAt(row))
                m_win->statusLabel()->setText(QStringLiteral("已删除书签"));
        } else if (chosen == clear) {
            clearBookmarksForCurrentMedia();
        }
    });

    registerActions();

    // 初始化 UI 状态
    controls->setRate(Settings::instance().defaultRate());
    controls->setVolume(Settings::instance().volume());
    controls->setLoaded(false);
    controls->setDuration(0);
    refreshVocabularyUi(); // M12：带读音/声调/词性（旧数据无读音则退回原样式）
    refreshFlashcardUi();
    refreshPlaylistUi();   // 上次退出时保留的播放列表
    refreshBookmarkUi();   // 无当前媒体 → 空列表
    rebuildBookmarkMenu();
    applyRetellSettings(); // 段落复述：段落句数/录音开关/麦克风探测
    refreshRetellUi();

    m_win->statusLabel()->setText(
        QStringLiteral("播放后端：%1").arg(m_engine->backendName()));
}

void AppController::registerActions()
{
    const QMap<QString, QKeySequence> defaults = Settings::defaultKeyBindings();
    auto add = [&](QMenu *menu, const QString &id, const QString &text, const char *slot) {
        QAction *a = new QAction(text, this);
        a->setShortcut(Settings::instance().keySequence(id));
        a->setShortcutContext(Qt::WindowShortcut);
        connect(a, SIGNAL(triggered()), this, slot);
        menu->addAction(a);
        return a;
    };

    QMenu *file = m_win->fileMenu();
    add(file, QStringLiteral("media.openFile"), QStringLiteral("打开文件…"),
        SLOT(openFileDialog()));
    add(file, QStringLiteral("media.openUrl"), QStringLiteral("打开在线视频…"),
        SLOT(openUrlDialog()));
    add(file, QStringLiteral("media.generateSubs"), QStringLiteral("ASR 生成字幕"),
        SLOT(generateSubtitlesAsr()));
    file->addSeparator();
    QAction *quit = file->addAction(QStringLiteral("退出"));
    connect(quit, &QAction::triggered, m_win, &QWidget::close);

    QMenu *study = m_win->studyMenu();
    add(study, QStringLiteral("play.pause"), QStringLiteral("播放/暂停"), SLOT(playPause()));
    add(study, QStringLiteral("play.stop"), QStringLiteral("停止"), SLOT(stopPlayback()));
    study->addSeparator();
    add(study, QStringLiteral("study.prevSentence"), QStringLiteral("上一句"), SLOT(prevSentence()));
    add(study, QStringLiteral("study.nextSentence"), QStringLiteral("下一句"), SLOT(nextSentence()));
    add(study, QStringLiteral("study.repeatSentence"), QStringLiteral("复读本句"), SLOT(repeatCurrentSentence()));
    add(study, QStringLiteral("study.abLoop"), QStringLiteral("A-B 循环"), SLOT(toggleAbLoop()));
    study->addSeparator();
    add(study, QStringLiteral("study.shadowing"), QStringLiteral("随意读（跟读）"), SLOT(startShadowing()));
    add(study, QStringLiteral("study.dictation"), QStringLiteral("听写"), SLOT(startDictation()));
    add(study, QStringLiteral("study.sentenceBuild"), QStringLiteral("造句练习"), SLOT(startSentenceBuild()));
    add(study, QStringLiteral("study.flashcard"), QStringLiteral("原句闪卡"), SLOT(startFlashcards()));
    add(study, QStringLiteral("study.retell"), QStringLiteral("段落复述"), SLOT(startRetell()));
    study->addSeparator();

    // 书签：固定项 + 动态书签清单（rebuildBookmarkMenu 维护分隔符之后的项）
    m_bookmarkMenu = study->addMenu(QStringLiteral("书签"));
    add(m_bookmarkMenu, QStringLiteral("bookmark.add"), QStringLiteral("新增书签…"), SLOT(addBookmark()));
    add(m_bookmarkMenu, QStringLiteral("bookmark.prev"), QStringLiteral("上一书签"), SLOT(jumpToPrevBookmark()));
    add(m_bookmarkMenu, QStringLiteral("bookmark.next"), QStringLiteral("下一书签"), SLOT(jumpToNextBookmark()));
    m_bookmarkDynSep = m_bookmarkMenu->addSeparator();

    QMenu *tools = m_win->toolsMenu();
    add(tools, QStringLiteral("play.forward5"), QStringLiteral("快进 5 秒"), SLOT(seekForward()));
    add(tools, QStringLiteral("play.back5"), QStringLiteral("后退 5 秒"), SLOT(seekBack()));
    add(tools, QStringLiteral("play.speedUp"), QStringLiteral("加速"), SLOT(speedUp()));
    add(tools, QStringLiteral("play.speedDown"), QStringLiteral("减速"), SLOT(speedDown()));
    add(tools, QStringLiteral("play.volumeUp"), QStringLiteral("音量+"), SLOT(volumeUp()));
    add(tools, QStringLiteral("play.volumeDown"), QStringLiteral("音量-"), SLOT(volumeDown()));
    add(tools, QStringLiteral("study.lookupWord"), QStringLiteral("查询当前词"), SLOT(lookupCurrentWord()));
    add(tools, QStringLiteral("study.soundSearch"), QStringLiteral("听音查字…"), SLOT(openSoundSearch()));
    add(tools, QStringLiteral("view.toggleTranscript"), QStringLiteral("显示/隐藏字幕侧栏"), SLOT(toggleTranscript()));

    QMenu *settingsMenu = m_win->settingsMenu();
    add(settingsMenu, QStringLiteral("settings.open"), QStringLiteral("设置…"), SLOT(openSettings()));

    Q_UNUSED(defaults);
}

// ---------- 播放 ----------

void AppController::openFileDialog()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        m_win, QStringLiteral("打开媒体"),
        QString(),
        QStringLiteral("媒体文件 (*.mp3 *.wav *.flac *.m4a *.aac *.ogg *.mp4 *.mkv *.avi *.mov *.webm);;所有文件 (*)"));
    if (files.isEmpty())
        return;
    for (const QString &f : files)
        m_playlist->addFile(f); // 去重：已在列表中的文件不会重复插入
    loadMedia(files.first(), false);
}

void AppController::openUrlDialog()
{
    bool ok = false;
    const QString url = QInputDialog::getText(m_win, QStringLiteral("打开在线视频"),
                                              QStringLiteral("视频 URL（支持 YouTube/B站等）:"),
                                              QLineEdit::Normal, QString(), &ok);
    if (ok && !url.trimmed().isEmpty())
        m_ytdlp->resolve(url.trimmed());
}

void AppController::generateSubtitlesAsr()
{
    if (m_currentMedia.isEmpty()) {
        m_win->statusLabel()->setText(QStringLiteral("请先打开媒体"));
        return;
    }
    if (m_isOnline) {
        m_win->statusLabel()->setText(
            QStringLiteral("在线媒体暂不支持本地 ASR：请先用 yt-dlp 下载或加载字幕"));
        return;
    }
    const QString engine = Settings::instance().asrEngine();
    AsrEngine::Options opt;
    opt.model = Settings::instance().whisperModelPath();
    opt.language = Settings::instance().asrLanguage();
    opt.outputDir = Settings::instance().cacheDir();
    m_asr->transcribe(m_currentMedia,
                      engine == QLatin1String("faster-whisper") ? AsrEngine::Engine::FasterWhisper
                                                                : AsrEngine::Engine::WhisperCpp,
                      opt);
}

void AppController::playPause()
{
    if (!m_engine->isLoaded())
        return;
    if (m_engine->isPlaying()) {
        m_engine->pause();
    } else {
        m_resumeSuspended = false; // 重新播放：恢复续播记录
        m_engine->play();
    }
}

void AppController::stopPlayback()
{
    m_repeat->stop();
    m_retell->stop(); // 播放归零会让复述段永远到不了段末 → 一并收尾（M12）
    saveResumeForCurrent(true); // 先记住停止前的位置（下面 seek(0) 会把它冲掉）
    m_resumeSuspended = true;   // 停止后位置归零：不再覆盖续播记录
    m_engine->pause();
    m_engine->seek(0);
}

void AppController::seekForward()
{
    seekRelative(5000);
}

void AppController::seekBack()
{
    seekRelative(-5000);
}

void AppController::seekRelative(Ms deltaMs)
{
    if (m_engine->isLoaded())
        m_engine->seek(qMax<Ms>(0, m_engine->position() + deltaMs));
}

void AppController::speedUp()
{
    const double r = qBound(0.25, m_engine->rate() + 0.1, 2.0);
    m_engine->setRate(r);
    m_win->playerControls()->setRate(r);
}

void AppController::speedDown()
{
    const double r = qBound(0.25, m_engine->rate() - 0.1, 2.0);
    m_engine->setRate(r);
    m_win->playerControls()->setRate(r);
}

void AppController::volumeUp()
{
    m_engine->setVolume(qMin(100, m_engine->volume() + 5));
}

void AppController::volumeDown()
{
    m_engine->setVolume(qMax(0, m_engine->volume() - 5));
}

void AppController::nextSentence()
{
    if (m_subtitles->count() == 0)
        return;
    const int idx = qBound(0, m_currentSentence + 1, m_subtitles->count() - 1);
    onSentenceActivated(idx);
}

void AppController::prevSentence()
{
    if (m_subtitles->count() == 0)
        return;
    const int idx = qBound(0, m_currentSentence - 1, m_subtitles->count() - 1);
    onSentenceActivated(idx);
}

void AppController::repeatCurrentSentence()
{
    if (m_currentSentence >= 0)
        startRepeatAt(m_currentSentence);
}

void AppController::startRepeatAt(int index)
{
    m_repeat->setConfig({Settings::instance().repeatCount(),
                         Settings::instance().repeatGapMs(), true});
    m_repeat->startSentence(index);
}

void AppController::toggleAbLoop()
{
    if (m_abB > m_abA && m_abA >= 0) {
        m_repeat->startAbLoop(m_abA, m_abB);
        m_win->statusLabel()->setText(QStringLiteral("A-B 复读 [%1, %2]").arg(m_abA).arg(m_abB));
    } else {
        m_abA = m_engine->position();
        m_abB = -1;
        m_win->statusLabel()->setText(QStringLiteral("请再标记 B 点"));
    }
}

void AppController::startShadowing()
{
    if (m_subtitles->count() == 0) {
        m_win->statusLabel()->setText(QStringLiteral("没有字幕/句子，无法跟读"));
        return;
    }
    // 手动模式交互：等待录音 → 按 F 开始；录音中 → 按 F 停止
    if (m_shadowing->state() == ShadowingSession::State::ListeningVoice) {
        m_shadowing->onUserStart();
        m_win->statusLabel()->setText(QStringLiteral("录音中…（再次按 F 停止）"));
        return;
    }
    if (m_shadowing->state() == ShadowingSession::State::Recording) {
        m_shadowing->onUserStop();
        return;
    }
    m_shadowing->begin(qMax(0, m_currentSentence), ShadowingMode::Manual);
    m_win->statusLabel()->setText(QStringLiteral("随意读：播放原音后按 F 开始录音（手动模式）"));
}

void AppController::startDictation()
{
    startDictationAt(qMax(0, m_currentSentence));
}

void AppController::startDictationAt(int index)
{
    if (m_subtitles->count() == 0)
        return;
    m_dictation->setMediaTitle(m_currentTitle);
    m_dictation->begin(DictationMode::Word, m_subtitles->sentences(), qMax(0, index),
                       Settings::instance().dictationIgnorePunctuation(),
                       Settings::instance().dictationIgnoreCase());
    m_win->rightTabs()->setCurrentIndex(1);
}

void AppController::startSentenceBuild()
{
    if (m_subtitles->count() == 0)
        return;
    m_win->sentenceBuilderWidget()->loadSentence(m_subtitles->at(qMax(0, m_currentSentence)).text);
    m_win->rightTabs()->setCurrentIndex(2);
}

void AppController::startFlashcards()
{
    if (m_currentSentence < 0 || m_currentSentence >= m_subtitles->count())
        return;
    m_flashcards->addClozeCardsFromSentence(m_subtitles->at(m_currentSentence), m_currentTitle, 2);
    m_win->statusLabel()->setText(QStringLiteral("已为本句生成 2 张原句闪卡"));
}

// ---------- 段落复述（M12） ----------

void AppController::applyRetellSettings()
{
    RetellSession::Config c = m_retell->config();
    c.micAvailable = AudioRecorder::inputAvailable(); // 无麦克风 → 会话自动走静默模式
    c.paragraphSize = Settings::instance().retellParagraphSize();
    c.recordAudio = Settings::instance().retellRecordEnabled();
    m_retell->setConfig(c);

    RetellWidget *rw = m_win->retellWidget();
    rw->setParagraphSize(c.paragraphSize);
    rw->setRecordEnabled(c.recordAudio);
    if (!c.micAvailable) {
        rw->setHint(QStringLiteral("未检测到麦克风：复述将走「静默回忆 → 揭晓对照 → 自评」"));
    }
}

void AppController::refreshRetellUi()
{
    RetellWidget *rw = m_win->retellWidget();
    QStringList items;
    QStringList tips;
    const QVector<RetellSession::Item> &h = m_retell->history();
    for (int i = h.size() - 1; i >= 0 && items.size() < 30; --i) {
        const RetellSession::Item &it = h.at(i);
        const QString when = QDateTime::fromMSecsSinceEpoch(it.at).toString(QStringLiteral("MM-dd hh:mm"));
        items << QStringLiteral("%1 · %2 · 第 %3–%4 句 · 自评 %5%6")
                     .arg(when, RetellSession::scopeDisplayName(RetellSession::scopeFromCode(it.scope)))
                     .arg(it.firstSentence + 1)
                     .arg(it.lastSentence + 1)
                     .arg(it.quality < 0 ? QStringLiteral("—") : QString::number(it.quality))
                     .arg(it.score >= 0 ? QStringLiteral(" · %1 分").arg(it.score) : QString());
        QString tip = QStringLiteral("媒体：%1\n范围：%2\n句区间：第 %3–%4 句\n自评：%5 / 5\n包络比对：%6")
                          .arg(it.media.isEmpty() ? QStringLiteral("（未知）") : it.media,
                               RetellSession::scopeDisplayName(RetellSession::scopeFromCode(it.scope)))
                          .arg(it.firstSentence + 1)
                          .arg(it.lastSentence + 1)
                          .arg(it.quality < 0 ? QStringLiteral("未评") : QString::number(it.quality))
                          .arg(it.score >= 0 ? QString::number(it.score) : QStringLiteral("未录音"));
        if (!it.detail.isEmpty())
            tip += QStringLiteral("\n细节：%1").arg(it.detail);
        if (!it.recordingPath.isEmpty())
            tip += QStringLiteral("\n录音：%1").arg(it.recordingPath);
        if (!it.text.isEmpty())
            tip += QStringLiteral("\n原文：%1").arg(it.text);
        tips << tip;
    }
    rw->showHistory(items, tips);
    rw->refresh();
}

void AppController::startRetell()
{
    // 已在练：再次触发即停止（与跟读 F 键同一手感）
    if (m_retell->state() != RetellSession::State::Idle
        && m_retell->state() != RetellSession::State::Finished) {
        m_retell->stop();
        return;
    }
    if (m_subtitles->count() == 0) {
        m_win->statusLabel()->setText(
            QStringLiteral("没有字幕/句子，无法复述（可 Ctrl+E 用 ASR 生成字幕）"));
        return;
    }
    if (!m_engine->isLoaded()) {
        // 没有媒体就没有播放位置回灌，会话会一直停在「播放原音」→ 不开会话
        m_win->statusLabel()->setText(QStringLiteral("请先打开媒体再开始复述"));
        return;
    }
    RetellWidget *rw = m_win->retellWidget();
    m_win->rightTabs()->setCurrentIndex(3); // 「复述」页签
    applyRetellSettings();
    m_retell->setMediaTitle(m_currentTitle);
    m_retell->setSentences(&m_subtitles->sentences());
    m_retell->beginWith(qMax(0, m_currentSentence), rw->scope(), rw->paragraphSize(),
                        rw->recordEnabled());
    refreshRetellUi();
}

void AppController::beginRetellCapture()
{
    if (m_recorder->startToBuffer()) {
        m_retellActive = true;
        m_win->statusLabel()->setText(QStringLiteral("复述录音中…说完点「说完了」"));
        return;
    }
    // 设备不可用：按「静默回忆」收尾，别让会话卡在录音态
    m_retellActive = false;
    m_win->statusLabel()->setText(QStringLiteral("无法启动录音设备：本次按静默回忆处理"));
    m_retell->onRecordingData({}, 16000, {}, 16000);
}

void AppController::stopRetellCapture(bool finalize)
{
    if (!m_retellActive) {
        if (finalize)
            m_retell->onRecordingData({}, 16000, {}, 16000); // 没录起来也要推进到揭晓
        return;
    }
    m_retellActive = false;
    const QByteArray raw = m_recorder->stopToBuffer();
    if (!finalize || raw.isEmpty()) {
        m_retell->onRecordingData({}, 16000, {}, 16000);
        return;
    }
    QDir().mkpath(Settings::instance().recordingsDir());
    const QString path = QDir(Settings::instance().recordingsDir())
                             .filePath(QStringLiteral("retell-%1.wav")
                                           .arg(QDateTime::currentDateTime()
                                                    .toString(QStringLiteral("yyyyMMdd-hhmmss-zzz"))));
    if (writeWav16kMono(path, raw))
        m_retell->setRecordingPath(path);

    const QVector<float> userPcm = s16leToFloat(raw);
    const RetellSession::Segment *seg = m_retell->currentSegment();
    if (!seg) {
        m_retell->onRecordingData(userPcm, 16000, {}, 16000);
        return;
    }
    // 原音区间由 ffmpeg 异步提取（与跟读同一链路），回来后做包络比对评分
    extractRangeAudio(seg->start, seg->end,
                      [this, userPcm](const QVector<float> &modelPcm, int sr) {
                          m_retell->onRecordingData(userPcm, 16000, modelPcm, sr);
                      });
}

void AppController::lookupCurrentWord()
{
    // 悬停中的词优先（M11：悬停/点击取词同一套分词结果）
    if (!m_hoverWord.isEmpty()) {
        lookupWord(m_hoverWord, m_hoverSentence);
        return;
    }
    if (m_currentSentence < 0 || m_currentSentence >= m_subtitles->count())
        return;
    const QStringList ws = m_subtitles->at(m_currentSentence).wordList();
    if (!ws.isEmpty())
        lookupWord(ws.first(), m_currentSentence);
}

void AppController::toggleTranscript()
{
    m_win->rightTabs()->setVisible(!m_win->rightTabs()->isVisible());
}

void AppController::openSettings()
{
    SettingsDialog dlg(m_win);
    if (dlg.exec() == QDialog::Accepted) {
        m_win->statusLabel()->setText(QStringLiteral("设置已保存"));
        // M11：语言/音量/分词路径可能变化 → 重新注入
        applyLanguageSettings();
        applyOverlaySettings();
        applyRetellSettings(); // M12：段落句数/录音开关
        m_pronunciation->setVolume(Settings::instance().volume());
        upgradeSentenceTokens(); // 新填了 MeCab / python 时立刻生效
        // 引擎路径可能变化：重新创建播放引擎
        MediaEngine *newEngine = MediaEngine::create(this);
        if (newEngine->backend() != m_engine->backend()) {
            delete m_engine;
            m_engine = newEngine;
            m_win->statusLabel()->setText(
                QStringLiteral("播放后端切换为：%1（重新打开媒体生效）").arg(m_engine->backendName()));
        } else {
            delete newEngine;
        }
    }
}

// ---------- 听音查字（M11） ----------

void AppController::openSoundSearch()
{
    if (!m_soundDialog) {
        m_soundDialog = new SoundSearchDialog(m_win);
        connect(m_soundDialog, &SoundSearchDialog::lookupRequested, this,
                [this](const QString &w) { lookupWord(w, m_currentSentence, QCursor::pos()); });
        connect(m_soundDialog, &SoundSearchDialog::recordToggled, this, [this](bool start) {
            if (start)
                startSoundSearchRecording();
            else
                stopSoundSearchRecording();
        });
    }
    m_soundDialog->show();
    m_soundDialog->raise();
    m_soundDialog->activateWindow();
    m_soundDialog->focusInput();
    m_win->statusLabel()->setText(QStringLiteral("听音查字：输入汉字/假名/罗马字，或点「录音」"));
}

void AppController::startSoundSearchRecording()
{
    QDir().mkpath(Settings::instance().recordingsDir());
    const QString path = QDir(Settings::instance().recordingsDir())
                             .filePath(QStringLiteral("soundsearch-%1.wav")
                                           .arg(QDateTime::currentDateTime()
                                                    .toString(QStringLiteral("yyyyMMdd-hhmmss"))));
    if (!m_recorder->start(path)) {
        if (m_soundDialog) {
            m_soundDialog->setRecording(false);
            m_soundDialog->setStatus(QStringLiteral("无法启动录音设备（请检查麦克风权限/输入设备）"));
        }
        return;
    }
    m_soundSearchWav = path;
    if (m_soundDialog)
        m_soundDialog->setStatus(QStringLiteral("录音中… 说完后点「停止并识别」"));
}

void AppController::stopSoundSearchRecording()
{
    if (!m_recorder->isRecording())
        return;
    m_recorder->stop(); // WAV 头由 AudioRecorder 回填
    const QString wav = m_soundSearchWav;
    m_soundSearchWav.clear();

    if (wav.isEmpty() || !QFileInfo::exists(wav) || QFileInfo(wav).size() <= 1024) {
        if (m_soundDialog)
            m_soundDialog->setStatus(QStringLiteral("录音过短或失败，请重试（也可直接输入查词）"));
        return;
    }

    AsrEngine::Options opt;
    opt.model = Settings::instance().whisperModelPath();
    opt.language = Settings::instance().asrLanguage(); // 默认跟随学习语言（日语）
    opt.outputDir = Settings::instance().cacheDir();
    const QString engine = Settings::instance().asrEngine();
    m_soundSearchTaskId = m_asr->transcribe(
        wav,
        engine == QLatin1String("faster-whisper") ? AsrEngine::Engine::FasterWhisper
                                                  : AsrEngine::Engine::WhisperCpp,
        opt);
    if (m_soundSearchTaskId == 0) {
        if (m_soundDialog)
            m_soundDialog->setStatus(QStringLiteral("无法提交识别任务"));
        return;
    }
    if (m_soundDialog) {
        m_soundDialog->setBusy(true);
        m_soundDialog->setStatus(QStringLiteral("识别中…（首次运行需加载模型，请稍候）"));
    }
}

// ---------- 内部 ----------

void AppController::loadMedia(const QString &path, bool isOnline, const QString &streamUrl)
{
    saveResumeForCurrent(true); // 切换前把上一个媒体的位置记下来
    m_retell->stop();           // 换媒体：复述会话收尾并恢复字幕显示（M12）

    m_currentMedia = path;
    m_currentTitle = QFileInfo(path).fileName();
    m_isOnline = isOnline;
    m_currentStreamUrl = streamUrl;
    m_resumeKey = mediaKey();
    m_resumeSuspended = false;
    m_lastResumePos = -1;

    // 播放列表：本地媒体登记（去重）；在线媒体地址不可复用，不入列表
    if (!isOnline)
        m_playlist->addFile(path);
    refreshPlaylistUi();
    // 书签按文件归属：切到当前文件的书签
    m_bookmarks->setMediaFilter(m_resumeKey);

    // 断点续播：记录始终保留，是否自动跳转由设置决定
    m_pendingResumeMs = 0;
    if (!isOnline && Settings::instance().resumeEnabled()) {
        const Ms saved = m_resume->position(m_resumeKey);
        if (saved >= ResumeStore::MinResumeMs)
            m_pendingResumeMs = saved;
    }

    m_subtitles->setSentences({});
    m_win->transcriptPanel()->setCurrentSentence(-1);
    m_currentSentence = -1;
    m_abA = -1;
    m_abB = -1;
    m_win->playerControls()->setAbMarkers(-1, -1);

    m_engine->open(streamUrl.isEmpty() ? path : streamUrl);
    m_engine->setRate(Settings::instance().defaultRate());
    m_engine->setVolume(Settings::instance().volume());

    // 波形（本地文件）
    if (!isOnline)
        m_waveform->generate(path, Settings::instance().cacheDir());

    // 字幕：自动探测同名字幕
    QStringList sidecars = SubtitleModel::probeSidecarSubs(path);
    if (!sidecars.isEmpty()) {
        loadSubtitles(sidecars.first());
    } else {
        m_win->statusLabel()->setText(
            QStringLiteral("无字幕：可 Ctrl+E 用 ASR 生成，或从侧栏加载"));
    }
}

void AppController::loadSubtitles(const QString &subPath)
{
    QString err;
    if (!m_subtitles->loadFile(subPath, &err)) {
        m_win->statusLabel()->setText(QStringLiteral("字幕加载失败：%1").arg(err));
        return;
    }
    // 双语：探测同目录 .zh.srt / 中文字幕
    const QFileInfo fi(subPath);
    const QString base = fi.completeBaseName();
    for (const QString &cand : {base + QStringLiteral(".zh.srt"),
                                base + QStringLiteral(".zh-cn.srt"),
                                base + QStringLiteral(".cn.srt")}) {
        const QString p = fi.dir().filePath(cand);
        if (QFileInfo::exists(p)) {
            SubtitleModel trans(this);
            if (trans.loadFile(p, nullptr))
                m_subtitles->mergeBilingual(trans);
            break;
        }
    }
    m_win->statusLabel()->setText(
        QStringLiteral("已加载字幕：%1 句").arg(m_subtitles->count()));
    upgradeSentenceTokens(); // 配了 MeCab/Python 桥则补分词（默认内置分词，直接返回）
    if (m_subtitles->count() > 0)
        maybeTranslateSubtitles();
}

// ---------- M11：语言与分词 ----------

Lang AppController::studyLang() const
{
    return Tokenizer::langFromCode(Settings::instance().studyLanguage());
}

void AppController::applyLanguageSettings()
{
    const Lang lang = studyLang();
    m_subtitles->setStudyLang(lang);
    m_win->sentenceBuilderWidget()->setLang(lang);
    m_dictation->setLang(lang);
    m_dictionary->setLanguage(lang); // 日语走 jc 节点，英语走 ec 节点
}

void AppController::applyOverlaySettings()
{
    SubtitleOverlay *ov = m_win->subtitleOverlay();
    if (!ov)
        return;
    ov->setHoverDelayMs(Settings::instance().dictHoverDelayMs());
    ov->setHoverLookupEnabled(Settings::instance().dictHoverLookupEnabled());
}

void AppController::upgradeSentenceTokens()
{
    const Lang lang = studyLang();
    if (lang != Lang::Japanese || m_subtitles->isEmpty())
        return; // 英语用不上外部分词器

    // 只有用户显式配置了 MeCab / python 才启动子进程；否则保持内置启发式（无开销）
    Tokenizer::Options opt;
    opt.mecabPath = Settings::instance().mecabPath();
    opt.pythonPath = Settings::instance().pythonPath();
    opt.timeoutMs = 8000;
    if (opt.mecabPath.trimmed().isEmpty() && opt.pythonPath.trimmed().isEmpty())
        return;

    QVector<Sentence> &ss = m_subtitles->mutableSentences();
    QStringList lines;
    for (const Sentence &s : ss)
        lines << s.text;
    QString engine;
    const QVector<QVector<Token>> batch =
        Tokenizer::tokensExternalBatch(lines, lang, opt, &engine);
    if (batch.size() != ss.size() || engine == QLatin1String("builtin")) {
        m_win->statusLabel()->setText(
            QStringLiteral("外部分词器不可用（MeCab/分词桥），已用内置日语分词"));
        return;
    }
    for (int i = 0; i < ss.size(); ++i) {
        ss[i].tokens = batch.at(i);
        // M12：外部分词器（MeCab/桥）给的读音也进读音缓存——
        // 只在词典没给过读音时补（查词结果优先级更高）
        m_readings->putFromTokens(batch.at(i), lang, false);
    }
    emit m_subtitles->changed();
    m_win->statusLabel()->setText(
        QStringLiteral("已用 %1 完成日语分词（%2 句）").arg(engine).arg(ss.size()));
}

void AppController::ensureSentences()
{
    // 无字幕时（未来：直接调用智能断句/ASR）；当前由 ASR 菜单触发。
}

void AppController::playRange(Ms start, Ms end)
{
    if (!m_engine->isLoaded())
        return;
    m_resumeSuspended = false;
    m_engine->seek(start);
    m_engine->play();
    m_activeRangeEnd = end;
}

void AppController::updatePosition(Ms pos)
{
    m_win->playerControls()->setPosition(pos);
    m_win->waveformWidget()->setPosition(pos);
    saveResumeForCurrent(false); // 每前进 5 秒落一次续播点（内部节流 + 立即落盘）

    if (m_subtitles->count() > 0) {
        int idx = -1;
        m_danmaku->tick(pos, m_subtitles->sentences(), &idx);
        if (idx != m_currentSentence) {
            m_currentSentence = idx;
            m_win->transcriptPanel()->setCurrentSentence(idx);
            m_win->waveformWidget()->setCurrentSentence(idx);
            m_win->subtitleOverlay()->setCurrentSentence(idx);
        }
    }

    m_repeat->tick(pos);
    m_dictation->onTick(pos);
    m_shadowing->tick(pos);
    m_retell->tick(pos);

    // 复读区间到点由 RepeatSession 内部处理
    if (m_repeat->state() == RepeatSession::State::Idle && m_activeRangeEnd > 0 && pos >= m_activeRangeEnd) {
        // 非复读的临时区间（如抠词复读）：到点暂停
        if (m_dictation->state() != DictationSession::State::Idle) {
            m_engine->pause();
        }
        m_activeRangeEnd = 0;
    }
}

void AppController::onSentenceActivated(int index)
{
    if (index < 0 || index >= m_subtitles->count())
        return;
    const Sentence &s = m_subtitles->at(index);
    m_resumeSuspended = false;
    m_engine->seek(s.start);
    if (m_engine->isLoaded())
        m_engine->play();
    m_win->transcriptPanel()->setCurrentSentence(index);
    m_win->waveformWidget()->setCurrentSentence(index);
    m_win->subtitleOverlay()->setCurrentSentence(index);
    m_currentSentence = index;
}

void AppController::lookupWord(const QString &word, int sentenceIndex, const QPoint &globalPos)
{
    Q_UNUSED(sentenceIndex);
    m_lookupPos = globalPos.isNull() ? QCursor::pos() : globalPos;
    m_lookupWordText = word;
    static quint64 token = 1;
    m_lookupToken = token++;
    m_dictPopup->showLoading(word, m_lookupPos);
    m_dictionary->lookup(word, m_lookupToken);
}

void AppController::addToVocabulary(const QString &word, const QString &sentence, const QString &source)
{
    // M12：把词典查到的读音（假名/声调/词性）一并落库；
    // 查词成功时已写入 ReadingStore，模型自己会补齐（此处显式传一份，便于单测与直调）
    ReadingInfo info = m_readings->info(word);
    if (info.lang.isEmpty())
        info.lang = Tokenizer::langCode(studyLang());
    m_vocabulary->addWord(word, sentence, source, info);
}

void AppController::addToSentenceBook(int index)
{
    if (index < 0 || index >= m_subtitles->count())
        return;
    const QString s = m_subtitles->at(index).text.trimmed();
    if (s.isEmpty() || m_sentenceBook.contains(s))
        return;
    m_sentenceBook << s;
    saveSentenceBook();
    m_win->statusLabel()->setText(QStringLiteral("已加入难重点句库（共 %1 句）").arg(m_sentenceBook.size()));
}

void AppController::saveAll()
{
    m_vocabulary->saveTo(m_vocabPath);
    m_flashcards->saveTo(m_flashcardPath);
    saveSentenceBook();
    m_playlist->saveTo(m_playlistPath);
    m_bookmarks->saveTo(m_bookmarkPath);
    m_resume->saveTo(m_resumePath);
    m_readings->saveTo(m_readingsPath); // M12 读音缓存
    m_retell->saveHistoryTo(m_retellPath); // M12 复述历史
}

// ---------- 日语化读音列表（M12） ----------

void AppController::refreshVocabularyUi()
{
    QStringList items;
    QStringList tips;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const VocabularyEntry &e : m_vocabulary->entries()) {
        // 日语生词：漢字 [かな] ② 名词 — 上下文；无读音时退回原样式（词 + 等级）
        QString line = ReadingStore::displayWithInfo(e.word, e.readingInfo());
        if (line == e.word)
            line = QStringLiteral("%1  [%2]").arg(e.word, QString::number(e.level));
        if (!e.contexts.isEmpty()) {
            QString ctx = e.contexts.first().first.trimmed().simplified();
            if (ctx.size() > 24)
                ctx = ctx.left(24) + QStringLiteral("…");
            if (!ctx.isEmpty())
                line += QStringLiteral(" — %1").arg(ctx);
        }
        items << line;

        QString tip = QStringLiteral("生词：%1").arg(e.word);
        if (!e.reading.isEmpty())
            tip += QStringLiteral("\n读音：%1%2").arg(e.reading, e.accent.isEmpty() ? QString() : QStringLiteral(" %1").arg(e.accent));
        if (!e.pos.isEmpty())
            tip += QStringLiteral("\n词性：%1").arg(e.pos);
        tip += QStringLiteral("\n等级：%1 / 5%2")
                   .arg(e.level)
                   .arg(e.due <= now ? QStringLiteral("（待复习）") : QString());
        tip += QStringLiteral("\n上下文 %1 条").arg(e.contexts.size());
        tips << tip;
    }
    m_win->refreshVocabularyList(items, tips);
}

void AppController::refreshFlashcardUi()
{
    QStringList desc;
    const QVector<Flashcard> &cards = m_flashcards->cards();
    for (const Flashcard &c : cards)
        desc << QStringLiteral("%1 → %2").arg(c.prompt, FlashcardModel::answerWithReading(c));
    m_win->refreshFlashcardList(desc);
}

void AppController::saveSentenceBook()
{
    // 句库由 addToSentenceBook 维护（内存 QVector），这里持久化
    QJsonArray arr;
    for (const QString &s : m_sentenceBook) {
        arr.append(s);
    }
    QFile f(m_sentenceBookPath);
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

// ---------- 播放列表 / 书签 / 断点续播（M10） ----------

QString AppController::mediaKey() const
{
    if (m_currentMedia.isEmpty())
        return {};
    // 本地媒体用规范化绝对路径（与书签/续播的归属键一致）；在线媒体没有稳定地址，用标题
    return m_isOnline ? m_currentTitle : PlaylistModel::normalizePath(m_currentMedia);
}

void AppController::refreshPlaylistUi()
{
    QStringList titles;
    QStringList tips;
    const QString cur = m_isOnline ? QString() : m_currentMedia;
    for (const PlaylistEntry &e : m_playlist->entries()) {
        titles << e.title;
        QString tip = e.path;
        if (e.durationMs > 0)
            tip += QStringLiteral("\n时长 %1").arg(timeutil::toClock(e.durationMs));
        if (!cur.isEmpty() && PlaylistModel::samePath(e.path, cur))
            tip += QStringLiteral("\n（当前播放）");
        tips << tip;
    }
    const int row = cur.isEmpty() ? -1 : m_playlist->indexOfPath(cur);
    m_win->refreshPlaylistList(titles, tips, row);
}

void AppController::openPlaylistRow(int row)
{
    const PlaylistEntry *e = m_playlist->at(row);
    if (!e)
        return;
    if (!QFileInfo::exists(e->path)) {
        m_win->statusLabel()->setText(
            QStringLiteral("文件不存在（可右键从列表移除）：%1").arg(e->path));
        return;
    }
    loadMedia(e->path, false);
}

void AppController::removePlaylistRow(int row)
{
    if (!m_playlist->removeAt(row))
        return;
    m_win->statusLabel()->setText(
        QStringLiteral("已从播放列表移除（书签与续播记录保留）"));
}

void AppController::clearPlaylist()
{
    if (m_playlist->entries().isEmpty())
        return;
    const auto ret = QMessageBox::question(
        m_win, QStringLiteral("清空播放列表"),
        QStringLiteral("确定清空播放列表？书签与续播记录不受影响。"));
    if (ret != QMessageBox::Yes)
        return;
    m_playlist->clear();
    m_win->statusLabel()->setText(QStringLiteral("播放列表已清空"));
}

void AppController::refreshBookmarkUi()
{
    QStringList items;
    QStringList tips;
    for (int row = 0; row < m_bookmarks->rowCount(); ++row) {
        const Bookmark *b = m_bookmarks->at(row);
        if (!b)
            continue;
        items << QStringLiteral("%1  %2").arg(timeutil::toClock(b->timeMs),
                                             b->hasNote() ? b->note : QStringLiteral("（无备注）"));
        QString tip = QStringLiteral("时间 %1").arg(timeutil::toClock(b->timeMs));
        if (b->hasNote())
            tip += QStringLiteral("\n备注 %1").arg(b->note);
        tip += QStringLiteral("\n媒体 %1").arg(b->mediaTitle.isEmpty() ? b->media : b->mediaTitle);
        tips << tip;
    }
    m_win->refreshBookmarkList(items, tips);
}

void AppController::rebuildBookmarkMenu()
{
    if (!m_bookmarkMenu || !m_bookmarkDynSep)
        return;
    // 只重建分隔符之后的动态项（固定项含快捷键，由 registerActions 注册）
    bool afterSep = false;
    const QList<QAction *> acts = m_bookmarkMenu->actions();
    for (QAction *a : acts) {
        if (a == m_bookmarkDynSep) {
            afterSep = true;
            continue;
        }
        if (afterSep) {
            m_bookmarkMenu->removeAction(a);
            a->deleteLater(); // 可能正处于该动作的触发栈中，延迟删除
        }
    }
    if (m_bookmarks->rowCount() == 0) {
        QAction *none = m_bookmarkMenu->addAction(QStringLiteral("（当前文件暂无书签）"));
        none->setEnabled(false);
        return;
    }
    for (int row = 0; row < m_bookmarks->rowCount(); ++row) {
        const Bookmark *b = m_bookmarks->at(row);
        if (!b)
            continue;
        const QString text = b->hasNote()
            ? QStringLiteral("%1  %2").arg(timeutil::toClock(b->timeMs), b->note)
            : QStringLiteral("%1  （无备注）").arg(timeutil::toClock(b->timeMs));
        QAction *a = m_bookmarkMenu->addAction(text);
        connect(a, &QAction::triggered, this, [this, row]() { jumpToBookmarkRow(row); });
    }
    m_bookmarkMenu->addSeparator();
    QAction *clear = m_bookmarkMenu->addAction(QStringLiteral("清空本文件书签"));
    connect(clear, &QAction::triggered, this, &AppController::clearBookmarksForCurrentMedia);
}

void AppController::addBookmark()
{
    if (m_resumeKey.isEmpty()) {
        m_win->statusLabel()->setText(QStringLiteral("请先打开媒体再添加书签"));
        return;
    }
    const Ms pos = m_engine->position();
    // 默认备注取当前句文本（直接回车即可加书签，不用手打）
    QString def;
    if (m_currentSentence >= 0 && m_currentSentence < m_subtitles->count())
        def = m_subtitles->at(m_currentSentence).text.trimmed();
    if (def.size() > 40)
        def = def.left(40) + QStringLiteral("…");

    bool ok = false;
    const QString note = QInputDialog::getText(
        m_win, QStringLiteral("新增书签"),
        QStringLiteral("备注（可留空）— 时间 %1：").arg(timeutil::toClock(pos)),
        QLineEdit::Normal, def, &ok);
    if (!ok)
        return;

    const int row = m_bookmarks->addBookmark(m_resumeKey, m_currentTitle, pos, note.trimmed());
    if (row < 0) {
        m_win->statusLabel()->setText(QStringLiteral("书签添加失败"));
        return;
    }
    m_win->bookmarkList()->setCurrentRow(row);
    m_win->statusLabel()->setText(
        QStringLiteral("已添加书签 @ %1（本文件共 %2 个）")
            .arg(timeutil::toClock(pos))
            .arg(m_bookmarks->rowCount()));
}

void AppController::jumpToBookmarkRow(int row)
{
    const Bookmark *b = m_bookmarks->at(row);
    if (!b)
        return;
    if (!m_engine->isLoaded()) {
        m_win->statusLabel()->setText(QStringLiteral("媒体未加载，无法跳转书签"));
        return;
    }
    m_resumeSuspended = false;
    m_engine->seek(b->timeMs);
    m_engine->play();
    m_lastResumePos = b->timeMs;
    m_win->bookmarkList()->setCurrentRow(row);
    m_win->statusLabel()->setText(
        QStringLiteral("跳转书签 @ %1%2")
            .arg(timeutil::toClock(b->timeMs),
                 b->hasNote() ? QStringLiteral(" · ") + b->note : QString()));
}

void AppController::jumpToNextBookmark()
{
    if (m_bookmarks->rowCount() == 0) {
        m_win->statusLabel()->setText(QStringLiteral("当前文件没有书签（Ctrl+B 新增）"));
        return;
    }
    const int row = m_bookmarks->nextRow(m_engine->position());
    if (row < 0) {
        m_win->statusLabel()->setText(QStringLiteral("已是最后一个书签"));
        return;
    }
    jumpToBookmarkRow(row);
}

void AppController::jumpToPrevBookmark()
{
    if (m_bookmarks->rowCount() == 0) {
        m_win->statusLabel()->setText(QStringLiteral("当前文件没有书签（Ctrl+B 新增）"));
        return;
    }
    const int row = m_bookmarks->prevRow(m_engine->position());
    if (row < 0) {
        m_win->statusLabel()->setText(QStringLiteral("已是第一个书签"));
        return;
    }
    jumpToBookmarkRow(row);
}

void AppController::clearBookmarksForCurrentMedia()
{
    const QString media = m_bookmarks->mediaFilter();
    if (media.isEmpty() || m_bookmarks->countForMedia(media) == 0) {
        m_win->statusLabel()->setText(QStringLiteral("当前文件没有书签"));
        return;
    }
    const auto ret = QMessageBox::question(m_win, QStringLiteral("清空书签"),
                                           QStringLiteral("确定删除当前文件的全部书签？"));
    if (ret != QMessageBox::Yes)
        return;
    const int n = m_bookmarks->clearMedia(media);
    m_win->statusLabel()->setText(QStringLiteral("已删除 %1 个书签").arg(n));
}

void AppController::saveResumeForCurrent(bool force)
{
    if (m_resumeKey.isEmpty() || m_resumeSuspended || m_isOnline || !m_engine)
        return;
    if (!m_engine->isLoaded())
        return;
    const Ms pos = m_engine->position();
    if (!force && m_lastResumePos >= 0 && qAbs(pos - m_lastResumePos) < kResumeCheckpointMs)
        return; // 未前进到下一个落点，避免高频写盘
    m_lastResumePos = pos;
    m_resume->record(m_resumeKey, pos, m_engine->duration());
}

void AppController::restoreResumeIfPending()
{
    if (m_pendingResumeMs <= 0 || !m_engine->isLoaded())
        return;
    const Ms pos = m_pendingResumeMs;
    m_pendingResumeMs = 0;
    m_engine->seek(pos);
    m_lastResumePos = pos;
    m_win->statusLabel()->setText(
        QStringLiteral("断点续播：从 %1 继续（可在「设置 → 学习」关闭）")
            .arg(timeutil::toClock(pos)));
}

void AppController::extractSentenceAudio(const Sentence &s,
                                         std::function<void(const QVector<float> &, int)> cb)
{
    extractRangeAudio(s.start, s.end, cb);
}

void AppController::extractRangeAudio(Ms start, Ms end,
                                      std::function<void(const QVector<float> &, int)> cb)
{
    // ffmpeg -ss/-to 提取区间 16kHz mono PCM（同步，短段可接受）
    QString ffmpeg = Settings::instance().ffmpegPath();
    if (ffmpeg.isEmpty())
        Subprocess::findExecutable(QStringLiteral("ffmpeg"), &ffmpeg);
    if (ffmpeg.isEmpty() || m_currentMedia.isEmpty() || m_isOnline) {
        cb({}, 16000);
        return;
    }
    const int timeout = int(qMax<Ms>(1000, end - start) + 30000);
    const Subprocess::Result r = Subprocess::run(
        ffmpeg,
        {QStringLiteral("-v"), QStringLiteral("error"),
         QStringLiteral("-ss"), QString::number(start / 1000.0, 'f', 3),
         QStringLiteral("-to"), QString::number(end / 1000.0, 'f', 3),
         QStringLiteral("-i"), m_currentMedia,
         QStringLiteral("-f"), QStringLiteral("s16le"),
         QStringLiteral("-ac"), QStringLiteral("1"),
         QStringLiteral("-ar"), QStringLiteral("16000"),
         QStringLiteral("-")},
        timeout);
    cb(s16leToFloat(r.ok ? r.stdoutData : QByteArray()), 16000);
}

void AppController::beginShadowingCapture()
{
    m_vadAccum.clear();
    if (m_recorder->startToBuffer()) {
        m_shadowingActive = true;
        if (m_shadowing->mode() == ShadowingMode::VoiceTriggered)
            m_vadTimer->start();
        m_win->statusLabel()->setText(QStringLiteral("录音中…（再次按 F 停止）"));
    } else {
        m_win->statusLabel()->setText(QStringLiteral("无法启动录音设备"));
    }
}

void AppController::stopShadowingCapture(bool finalize)
{
    m_vadTimer->stop();
    if (!m_shadowingActive)
        return;
    m_shadowingActive = false;
    const QByteArray raw = m_recorder->stopToBuffer();
    if (!finalize || raw.isEmpty()) {
        m_shadowing->onRecordingData({}, 16000, {}, 16000);
        return;
    }
    // 保存录音（写合法 WAV：44 字节头 + PCM）
    QDir().mkpath(Settings::instance().recordingsDir());
    m_pendingRecordingPath = QDir(Settings::instance().recordingsDir())
                                 .filePath(QStringLiteral("shadow-%1.wav")
                                               .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz"))));
    QFile f(m_pendingRecordingPath);
    if (f.open(QIODevice::WriteOnly)) {
        // 16kHz / 单声道 / 16bit 头
        const quint32 dataLen = quint32(raw.size());
        const quint32 byteRate = 16000u * 1u * 16u / 8u;
        QByteArray h(44, '\0');
        auto put32 = [&](qint64 off, quint32 v) {
            h[off] = char(v & 0xFF);
            h[off + 1] = char((v >> 8) & 0xFF);
            h[off + 2] = char((v >> 16) & 0xFF);
            h[off + 3] = char((v >> 24) & 0xFF);
        };
        memcpy(h.data(), "RIFF", 4);
        put32(4, 36 + dataLen);
        memcpy(h.data() + 8, "WAVE", 4);
        memcpy(h.data() + 12, "fmt ", 4);
        put32(16, 16);
        h[20] = 1;
        h[21] = 0;
        h[22] = 1;
        h[23] = 0;
        put32(24, 16000);
        put32(28, byteRate);
        h[32] = 2;
        h[33] = 0;
        h[34] = 16;
        h[35] = 0;
        memcpy(h.data() + 36, "data", 4);
        put32(40, dataLen);
        f.write(h);
        f.write(raw);
        f.close();
    }
    const QVector<float> userPcm = s16leToFloat(raw);

    // 取模型音比对
    if (m_currentSentence >= 0 && m_currentSentence < m_subtitles->count()) {
        extractSentenceAudio(m_subtitles->at(m_currentSentence),
                             [this, userPcm](const QVector<float> &modelPcm, int sr) {
                                 m_shadowing->onRecordingData(userPcm, 16000, modelPcm, sr);
                             });
    } else {
        m_shadowing->onRecordingData(userPcm, 16000, {}, 16000);
    }
}

void AppController::onVadTick()
{
    if (!m_shadowingActive || !m_recorder->isRecording())
        return;
    const QByteArray chunk = m_recorder->drainBuffer();
    if (chunk.isEmpty())
        return;
    m_vadAccum.append(chunk);
    // 只分析最近 500ms，避免累积膨胀
    const int keep = m_recorder->sampleRate() * 2;
    if (m_vadAccum.size() > keep)
        m_vadAccum = m_vadAccum.right(keep);

    const QVector<float> f = s16leToFloat(m_vadAccum);
    const Ms onset = AudioAnalysis::voiceOnsetMs(f, m_recorder->sampleRate(), 0.05);
    m_shadowing->feedVoiceDetected(onset >= 0);
}

void AppController::onShadowingResult(int index, const ShadowingScore &score, const QString &recPath)
{
    m_win->statusLabel()->setText(
        QStringLiteral("跟读（句 %1）：得分 %2 分 · %3").arg(index + 1).arg(score.score).arg(score.detail));
    if (!recPath.isEmpty())
        m_win->statusLabel()->setText(m_win->statusLabel()->text() + QStringLiteral(" · 录音已保存"));
}

void AppController::maybeTranslateSubtitles()
{
    // 批量补译文：无 translation 的句子逐句请求（token 关联句索引）
    if (m_subtitles->isEmpty())
        return;
    static quint64 transToken = 1;
    const QString from = Settings::instance().sourceLang();
    const QString to = Settings::instance().targetLang();
    const QVector<Sentence> &ss = m_subtitles->sentences();
    for (int i = 0; i < ss.size(); ++i) {
        if (!ss[i].translation.isEmpty())
            continue;
        const QString text = ss[i].text.trimmed();
        if (text.size() < 2)
            continue;
        const quint64 t = transToken++;
        m_pendingTrans.insert(t, i);
        m_translator->translate(text, from, to, t);
    }
    m_win->statusLabel()->setText(
        QStringLiteral("已请求 %1 句译文…").arg(m_pendingTrans.size()));
}

} // namespace adoloop
