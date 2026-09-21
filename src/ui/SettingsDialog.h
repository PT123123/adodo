#pragma once

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QSpinBox;
class QTableWidget;
class QTabWidget;
class QCheckBox;

namespace adoloop {

// 设置对话框：通用 / 播放 / ASR / 断句 / 学习 / 快捷键。
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

signals:
    void settingsSaved();

private:
    void buildGeneralTab();
    void buildSegmentTab();
    void buildStudyTab();
    void buildKeysTab();
    void apply();
    void detectConflicts();

    QTabWidget *m_tabs = nullptr;

    // 通用
    QLineEdit *m_dataDir = nullptr;
    QLineEdit *m_mpvPath = nullptr;
    QLineEdit *m_ffmpegPath = nullptr;
    QLineEdit *m_ytDlpPath = nullptr;
    QLineEdit *m_whisperCliPath = nullptr;
    QLineEdit *m_pythonPath = nullptr;
    QLineEdit *m_whisperModelPath = nullptr;
    QLineEdit *m_mecabPath = nullptr;   // 日语分词增强（可选）
    QComboBox *m_asrEngine = nullptr;
    QCheckBox *m_asrLangFollow = nullptr;
    QLineEdit *m_asrLang = nullptr;

    // 断句
    QDoubleSpinBox *m_noise = nullptr;
    QSpinBox *m_minGap = nullptr;
    QSpinBox *m_minSentence = nullptr;
    QSpinBox *m_allowedNoise = nullptr;
    QCheckBox *m_removeSilence = nullptr;

    // 学习
    QSpinBox *m_repeatCount = nullptr;
    QSpinBox *m_repeatGap = nullptr;
    QCheckBox *m_ignorePunct = nullptr;
    QCheckBox *m_ignoreCase = nullptr;
    QCheckBox *m_resumePlay = nullptr;
    QComboBox *m_studyLang = nullptr;   // 学习语言（日本語 / English）
    QComboBox *m_sourceLang = nullptr;  // 源语言（下拉）
    QLineEdit *m_targetLang = nullptr;
    QSpinBox *m_hoverDelay = nullptr;
    QCheckBox *m_hoverLookup = nullptr;
    QSpinBox *m_retellParagraphSize = nullptr; // 段落复述：段落句数（M12）
    QCheckBox *m_retellRecord = nullptr;       // 段落复述：是否录音（M12）

    // 快捷键
    QTableWidget *m_keysTable = nullptr;
};

} // namespace adoloop
