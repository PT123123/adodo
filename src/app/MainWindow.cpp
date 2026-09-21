#include "MainWindow.h"
#include "../ui/DanmakuWidget.h"
#include "../ui/DictationWidget.h"
#include "../ui/PlayerControls.h"
#include "../ui/RepeatControls.h"
#include "../ui/RetellWidget.h"
#include "../ui/SentenceBuilderWidget.h"
#include "../ui/SubtitleOverlay.h"
#include "../ui/TranscriptPanel.h"
#include "../ui/WaveformWidget.h"

#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QVBoxLayout>

namespace adoloop {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("AdoLoop · 外语复读学习机"));
    resize(1280, 800);
    buildUi();
    buildMenus();
    statusBar()->addWidget(m_status);
}

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---------- 左：播放列表 / 书签 / 生词本 / 闪卡 ----------
    m_leftTabs = new QTabWidget(central);
    m_playlist = new QListWidget(m_leftTabs);
    m_bookmarkList = new QListWidget(m_leftTabs);
    m_vocabularyList = new QListWidget(m_leftTabs);
    m_flashcardList = new QListWidget(m_leftTabs);
    // 右键菜单（移除/清空/跳转/删除）由 AppController 装配
    m_playlist->setContextMenuPolicy(Qt::CustomContextMenu);
    m_bookmarkList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_leftTabs->addTab(m_playlist, QStringLiteral("播放列表"));
    m_leftTabs->addTab(m_bookmarkList, QStringLiteral("书签"));
    m_leftTabs->addTab(m_vocabularyList, QStringLiteral("生词本"));
    m_leftTabs->addTab(m_flashcardList, QStringLiteral("闪卡"));
    m_leftTabs->setMinimumWidth(180);
    m_leftTabs->setMaximumWidth(260);

    // ---------- 中央：视频区 + 字幕/弹幕浮层 + 波形 + 控制 ----------
    auto *center = new QWidget(central);
    auto *centerLay = new QVBoxLayout(center);
    centerLay->setContentsMargins(0, 0, 0, 0);
    centerLay->setSpacing(0);

    m_mediaHost = new QWidget(center);
    m_mediaHost->setMinimumHeight(280);
    m_mediaHost->setStyleSheet(QStringLiteral("background:#14161b;"));
    auto *hostLay = new QVBoxLayout(m_mediaHost);
    hostLay->setContentsMargins(0, 0, 0, 0);

    m_subtitleOverlay = new SubtitleOverlay(m_mediaHost);
    m_danmakuWidget = new DanmakuWidget(m_mediaHost);
    hostLay->addWidget(m_subtitleOverlay, 1);
    hostLay->addWidget(m_danmakuWidget, 0);
    m_danmakuWidget->setEnabled2(false);
    m_subtitleOverlay->lower();

    m_waveform = new WaveformWidget(center);
    m_controls = new PlayerControls(center);
    m_repeatControls = new RepeatControls(center);

    centerLay->addWidget(m_mediaHost, 1);
    centerLay->addWidget(m_waveform);
    centerLay->addWidget(m_repeatControls);
    centerLay->addWidget(m_controls);

    // ---------- 右：字幕侧栏 + 学习面板 ----------
    m_rightTabs = new QTabWidget(central);
    m_transcript = new TranscriptPanel(m_rightTabs);
    m_dictation = new DictationWidget(m_rightTabs);
    m_sentenceBuilder = new SentenceBuilderWidget(m_rightTabs);
    m_retell = new RetellWidget(m_rightTabs);
    m_rightTabs->addTab(m_transcript, QStringLiteral("字幕"));
    m_rightTabs->addTab(m_dictation, QStringLiteral("听写"));
    m_rightTabs->addTab(m_sentenceBuilder, QStringLiteral("造句"));
    m_rightTabs->addTab(m_retell, QStringLiteral("复述"));
    m_rightTabs->setMinimumWidth(240);
    m_rightTabs->setMaximumWidth(420);

    // ---------- 分栏 ----------
    auto *split = new QSplitter(Qt::Horizontal, central);
    split->addWidget(m_leftTabs);
    split->addWidget(center);
    split->addWidget(m_rightTabs);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setStretchFactor(2, 0);
    split->setSizes({200, 760, 320});
    root->addWidget(split);

    m_status = new QLabel(QStringLiteral("就绪"), this);
}

void MainWindow::buildMenus()
{
    QMenuBar *bar = menuBar();
    m_fileMenu = bar->addMenu(QStringLiteral("文件(&F)"));
    m_studyMenu = bar->addMenu(QStringLiteral("学习(&L)"));
    m_toolsMenu = bar->addMenu(QStringLiteral("工具(&T)"));
    m_settingsMenu = bar->addMenu(QStringLiteral("设置(&S)"));
}

void MainWindow::refreshPlaylistList(const QStringList &titles, const QStringList &tooltips, int currentRow)
{
    m_playlist->clear();
    for (int i = 0; i < titles.size(); ++i) {
        auto *item = new QListWidgetItem(titles[i], m_playlist);
        item->setToolTip(i < tooltips.size() ? tooltips[i] : QString());
    }
    if (currentRow >= 0 && currentRow < m_playlist->count())
        m_playlist->setCurrentRow(currentRow);
}

void MainWindow::refreshBookmarkList(const QStringList &descriptions, const QStringList &tooltips)
{
    m_bookmarkList->clear();
    for (int i = 0; i < descriptions.size(); ++i) {
        auto *item = new QListWidgetItem(descriptions[i], m_bookmarkList);
        item->setToolTip(i < tooltips.size() ? tooltips[i] : QString());
    }
}

void MainWindow::refreshVocabularyList(const QStringList &items, const QStringList &tooltips)
{
    m_vocabularyList->clear();
    for (int i = 0; i < items.size(); ++i) {
        auto *item = new QListWidgetItem(items[i], m_vocabularyList);
        if (i < tooltips.size())
            item->setToolTip(tooltips[i]);
    }
}

void MainWindow::refreshFlashcardList(const QStringList &descriptions)
{
    m_flashcardList->clear();
    m_flashcardList->addItems(descriptions);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // 由 AppController 通过 QObject::destroyed 保存数据；
    // 此处仅接受关闭。
    QMainWindow::closeEvent(event);
}

} // namespace adoloop
