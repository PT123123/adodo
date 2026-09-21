set(CMAKE_AUTOMOC ON)

set(ADOLOOP_SOURCES
    main.cpp

    app/AppController.cpp
    app/MainWindow.cpp
    app/Settings.cpp

    core/Sentence.cpp
    core/Tokenizer.cpp
    core/SubtitleModel.cpp
    core/PronunciationPlayer.cpp   # 词典发音（独立 QMediaPlayer，不干扰 mpv）
    core/MediaEngine.cpp
    core/MpvMediaEngine.cpp
    core/QtMediaEngine.cpp
    core/Waveform.cpp
    core/AudioRecorder.cpp
    core/AudioAnalysis.cpp
    core/PlaylistModel.cpp
    core/BookmarkModel.cpp
    core/ResumeStore.cpp
    core/ReadingStore.cpp          # 读音缓存（M12：查词得到的假名/声调/词性落盘复用）

    subtitle/SubtitleParser.cpp
    subtitle/SubtitleAligner.cpp
    subtitle/TranslationProvider.cpp

    asr/AsrEngine.h   # 纯接口头（Q_OBJECT），无同名 .cpp，需列入源清单供 AUTOMOC 处理
    asr/WhisperCppAsr.cpp
    asr/FasterWhisperAsr.cpp
    asr/AsrTaskManager.cpp

    study/RepeatSession.cpp
    study/ShadowingSession.cpp
    study/DictationSession.cpp
    study/RetellSession.cpp      # 段落复述状态机（M12：句/意群/段落/全文 + 无麦克风降级）
    study/SentenceBuilder.cpp
    study/FlashcardModel.cpp
    study/VocabularyModel.cpp
    study/DanmakuController.cpp

    net/HttpClient.cpp
    net/YtDlpRunner.cpp
    net/DictionaryParser.cpp   # 有道 jsonapi 解析（日语 jc / 英语 ec，纯函数可单测）
    net/DictionaryProvider.cpp

    ui/PlayerControls.cpp
    ui/WaveformWidget.cpp
    ui/TranscriptPanel.cpp
    ui/SubtitleOverlay.cpp
    ui/DanmakuWidget.cpp
    ui/DictionaryPopup.cpp
    ui/RepeatControls.cpp
    ui/DictationWidget.cpp
    ui/SentenceBuilderWidget.cpp
    ui/RetellWidget.cpp          # 段落复述面板（M12）
    ui/SettingsDialog.cpp
    ui/SoundSearchDialog.cpp

    util/Subprocess.cpp
    util/TimeUtil.cpp
    util/StringUtil.cpp
    util/JapaneseInput.cpp
)

add_executable(AdoLoop WIN32 ${ADOLOOP_SOURCES})

target_include_directories(AdoLoop PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(AdoLoop PRIVATE
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::Network
    Qt6::Concurrent
)

set_target_properties(AdoLoop PROPERTIES
    WIN32_EXECUTABLE TRUE
)
