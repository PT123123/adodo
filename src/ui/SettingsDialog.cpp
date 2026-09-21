#include "SettingsDialog.h"
#include "../app/Settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace adoloop {

namespace {
// 学习/源语言下拉：显示名 ↔ 语言代码（"ja" / "en"）
void fillLangCombo(QComboBox *box)
{
    box->clear();
    box->addItem(QStringLiteral("日本語"), QStringLiteral("ja"));
    box->addItem(QStringLiteral("English"), QStringLiteral("en"));
}

int langComboIndex(const QString &code)
{
    return code.trimmed().startsWith(QLatin1String("en"), Qt::CaseInsensitive) ? 1 : 0;
}
} // namespace

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("设置"));
    resize(560, 520);

    auto *lay = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);
    lay->addWidget(m_tabs);

    buildGeneralTab();
    buildSegmentTab();
    buildStudyTab();
    buildKeysTab();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    lay->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        apply();
        accept();
        emit settingsSaved();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void SettingsDialog::buildGeneralTab()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);

    Settings &s = Settings::instance();
    m_dataDir = new QLineEdit(s.dataDir(), page);

    auto makePathRow = [&](const QString &label, QLineEdit *&ed, const QString &value) {
        ed = new QLineEdit(value, page);
        auto *browse = new QPushButton(QStringLiteral("…"), page);
        connect(browse, &QPushButton::clicked, this, [this, ed]() {
            const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("选择程序"));
            if (!f.isEmpty())
                ed->setText(f);
        });
        auto *row = new QHBoxLayout();
        row->addWidget(ed, 1);
        row->addWidget(browse);
        auto *w = new QWidget(page);
        w->setLayout(row);
        form->addRow(label, w);
    };

    form->addRow(QStringLiteral("数据目录"), m_dataDir);
    makePathRow(QStringLiteral("mpv"), m_mpvPath, s.mpvPath());
    makePathRow(QStringLiteral("ffmpeg"), m_ffmpegPath, s.ffmpegPath());
    makePathRow(QStringLiteral("yt-dlp"), m_ytDlpPath, s.ytDlpPath());
    makePathRow(QStringLiteral("whisper-cli"), m_whisperCliPath, s.whisperCliPath());
    makePathRow(QStringLiteral("python"), m_pythonPath, s.pythonPath());
    makePathRow(QStringLiteral("whisper 模型"), m_whisperModelPath, s.whisperModelPath());
    makePathRow(QStringLiteral("MeCab（日语分词，可选）"), m_mecabPath, s.mecabPath());

    m_asrEngine = new QComboBox(page);
    m_asrEngine->addItems({QStringLiteral("whisper.cpp"), QStringLiteral("faster-whisper")});
    m_asrEngine->setCurrentText(s.asrEngine());
    form->addRow(QStringLiteral("ASR 引擎"), m_asrEngine);

    m_asrLangFollow = new QCheckBox(QStringLiteral("跟随学习语言"), page);
    m_asrLangFollow->setChecked(s.asrLangFollowsStudy());
    m_asrLang = new QLineEdit(s.asrLanguageManual(), page);
    m_asrLang->setEnabled(!s.asrLangFollowsStudy());
    connect(m_asrLangFollow, &QCheckBox::toggled, m_asrLang,
            [this](bool on) { m_asrLang->setEnabled(!on); });
    {
        auto *row = new QHBoxLayout();
        row->addWidget(m_asrLangFollow);
        row->addWidget(m_asrLang, 1);
        auto *w = new QWidget(page);
        w->setLayout(row);
        form->addRow(QStringLiteral("ASR 语言"), w);
    }

    auto *hint = new QLabel(
        QStringLiteral("路径留空 = 在 PATH 中自动查找；MeCab 与 python(janome/fugashi) 均为可选增强，"
                       "缺失时自动使用内置日语分词"), page);
    hint->setStyleSheet(QStringLiteral("color:#888;"));
    hint->setWordWrap(true);
    form->addRow(hint);

    m_tabs->addTab(page, QStringLiteral("通用 / 引擎"));
}

void SettingsDialog::buildSegmentTab()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);
    const SegmentParams p = Settings::instance().segmentParams();

    m_noise = new QDoubleSpinBox(page);
    m_noise->setRange(0.001, 0.5);
    m_noise->setSingleStep(0.005);
    m_noise->setDecimals(3);
    m_noise->setValue(p.noiseThreshold);
    form->addRow(QStringLiteral("背景噪音阈值"), m_noise);

    m_minGap = new QSpinBox(page);
    m_minGap->setRange(50, 3000);
    m_minGap->setSingleStep(20);
    m_minGap->setValue(p.minGapMs);
    form->addRow(QStringLiteral("句间停顿 (ms)"), m_minGap);

    m_minSentence = new QSpinBox(page);
    m_minSentence->setRange(100, 5000);
    m_minSentence->setSingleStep(50);
    m_minSentence->setValue(p.minSentenceMs);
    form->addRow(QStringLiteral("最短句长 (ms)"), m_minSentence);

    m_allowedNoise = new QSpinBox(page);
    m_allowedNoise->setRange(0, 500);
    m_allowedNoise->setSingleStep(10);
    m_allowedNoise->setValue(p.allowedNoiseMs);
    form->addRow(QStringLiteral("允许杂音数 (ms)"), m_allowedNoise);

    m_removeSilence = new QCheckBox(page);
    m_removeSilence->setChecked(p.removeSilence);
    form->addRow(QStringLiteral("静音去除"), m_removeSilence);

    auto *hint = new QLabel(QStringLiteral("参考 Aboboo：不同音频需不同参数；波形上可局部重断句"), page);
    hint->setStyleSheet(QStringLiteral("color:#888;"));
    form->addRow(hint);

    m_tabs->addTab(page, QStringLiteral("智能断句"));
}

void SettingsDialog::buildStudyTab()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);
    Settings &s = Settings::instance();

    m_repeatCount = new QSpinBox(page);
    m_repeatCount->setRange(1, 20);
    m_repeatCount->setValue(s.repeatCount());
    form->addRow(QStringLiteral("默认重复次数"), m_repeatCount);

    m_repeatGap = new QSpinBox(page);
    m_repeatGap->setRange(0, 5000);
    m_repeatGap->setSingleStep(100);
    m_repeatGap->setValue(s.repeatGapMs());
    form->addRow(QStringLiteral("默认间隔 (ms)"), m_repeatGap);

    m_ignorePunct = new QCheckBox(page);
    m_ignorePunct->setChecked(s.dictationIgnorePunctuation());
    form->addRow(QStringLiteral("听写忽略标点"), m_ignorePunct);

    m_ignoreCase = new QCheckBox(page);
    m_ignoreCase->setChecked(s.dictationIgnoreCase());
    form->addRow(QStringLiteral("听写忽略大小写"), m_ignoreCase);

    m_resumePlay = new QCheckBox(page);
    m_resumePlay->setChecked(s.resumeEnabled());
    form->addRow(QStringLiteral("断点续播"), m_resumePlay);

    auto *resumeHint = new QLabel(
        QStringLiteral("记住每个文件上次播放位置，再次打开时自动续播；关闭后仍保留记录，只是不跳转"), page);
    resumeHint->setStyleSheet(QStringLiteral("color:#888;"));
    resumeHint->setWordWrap(true);
    form->addRow(resumeHint);

    m_sourceLang = new QComboBox(page);
    fillLangCombo(m_studyLang = new QComboBox(page));
    fillLangCombo(m_sourceLang);
    m_studyLang->setCurrentIndex(langComboIndex(s.studyLanguage()));
    m_sourceLang->setCurrentIndex(langComboIndex(s.sourceLang()));
    form->addRow(QStringLiteral("学习语言"), m_studyLang);
    form->addRow(QStringLiteral("源语言（字幕/翻译）"), m_sourceLang);
    auto *langHint = new QLabel(
        QStringLiteral("学习语言默认「日本語」：分词、词典解析、发音都按它走；"
                       "源语言与 ASR 语言默认跟随学习语言"), page);
    langHint->setStyleSheet(QStringLiteral("color:#888;"));
    langHint->setWordWrap(true);
    form->addRow(langHint);

    m_targetLang = new QLineEdit(s.targetLang(), page);
    form->addRow(QStringLiteral("目标语言 (如 zh)"), m_targetLang);

    m_hoverLookup = new QCheckBox(QStringLiteral("悬停即查词（关闭则为仅点击查词）"), page);
    m_hoverLookup->setChecked(s.dictHoverLookupEnabled());
    form->addRow(QStringLiteral("字幕取词"), m_hoverLookup);

    m_hoverDelay = new QSpinBox(page);
    m_hoverDelay->setRange(0, 3000);
    m_hoverDelay->setSingleStep(50);
    m_hoverDelay->setSuffix(QStringLiteral(" ms"));
    m_hoverDelay->setValue(s.dictHoverDelayMs());
    form->addRow(QStringLiteral("悬停触发延时"), m_hoverDelay);

    // M12：段落复述
    m_retellParagraphSize = new QSpinBox(page);
    m_retellParagraphSize->setRange(1, 20);
    m_retellParagraphSize->setSuffix(QStringLiteral(" 句"));
    m_retellParagraphSize->setValue(s.retellParagraphSize());
    form->addRow(QStringLiteral("复述段落长度"), m_retellParagraphSize);

    m_retellRecord = new QCheckBox(QStringLiteral("复述时录音（关闭 = 静默回忆模式）"), page);
    m_retellRecord->setChecked(s.retellRecordEnabled());
    form->addRow(QStringLiteral("段落复述"), m_retellRecord);
    auto *retellHint = new QLabel(
        QStringLiteral("段落复述：听原音 → 隐藏原文回忆 → 复述（可选录音）→ 揭晓对照 → 自评；"
                       "没有麦克风时自动降级为「静默回忆 → 揭晓对照 → 自评」"), page);
    retellHint->setStyleSheet(QStringLiteral("color:#888;"));
    retellHint->setWordWrap(true);
    form->addRow(retellHint);

    m_tabs->addTab(page, QStringLiteral("学习"));
}

void SettingsDialog::buildKeysTab()
{
    auto *page = new QWidget(this);
    auto *lay = new QVBoxLayout(page);

    m_keysTable = new QTableWidget(page);
    m_keysTable->setColumnCount(3);
    m_keysTable->setHorizontalHeaderLabels(
        {QStringLiteral("动作"), QStringLiteral("键位"), QStringLiteral("默认")});
    m_keysTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_keysTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_keysTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_keysTable->verticalHeader()->setVisible(false);
    lay->addWidget(m_keysTable);

    const auto defaults = Settings::defaultKeyBindings();
    const auto current = Settings::instance().allKeyBindings();
    m_keysTable->setRowCount(defaults.size());
    int r = 0;
    for (auto it = defaults.begin(); it != defaults.end(); ++it, ++r) {
        auto *name = new QTableWidgetItem(Settings::actionDisplayName(it.key()));
        name->setFlags(name->flags() & ~Qt::ItemIsEditable);
        m_keysTable->setItem(r, 0, name);

        auto *edit = new QKeySequenceEdit(current.value(it.key()), page);
        edit->setProperty("actionId", it.key());
        m_keysTable->setCellWidget(r, 1, edit);

        auto *def = new QTableWidgetItem(it.value().toString(QKeySequence::PortableText));
        def->setFlags(def->flags() & ~Qt::ItemIsEditable);
        m_keysTable->setItem(r, 2, def);
    }

    auto *restore = new QPushButton(QStringLiteral("恢复默认键位"), page);
    connect(restore, &QPushButton::clicked, this, [this]() {
        for (int r = 0; r < m_keysTable->rowCount(); ++r) {
            if (auto *edit = qobject_cast<QKeySequenceEdit *>(m_keysTable->cellWidget(r, 1))) {
                if (auto *def = m_keysTable->item(r, 2))
                    edit->setKeySequence(QKeySequence::fromString(
                        def->text(), QKeySequence::PortableText));
            }
        }
    });
    lay->addWidget(restore);

    m_tabs->addTab(page, QStringLiteral("快捷键"));
}

void SettingsDialog::apply()
{
    Settings &s = Settings::instance();
    s.setDataDir(m_dataDir->text().trimmed());
    s.setMpvPath(m_mpvPath->text().trimmed());
    s.setFfmpegPath(m_ffmpegPath->text().trimmed());
    s.setYtDlpPath(m_ytDlpPath->text().trimmed());
    s.setWhisperCliPath(m_whisperCliPath->text().trimmed());
    s.setPythonPath(m_pythonPath->text().trimmed());
    s.setWhisperModelPath(m_whisperModelPath->text().trimmed());
    s.setMecabPath(m_mecabPath->text().trimmed());
    s.setAsrEngine(m_asrEngine->currentText());
    s.setAsrLangFollowsStudy(m_asrLangFollow->isChecked());
    if (!m_asrLangFollow->isChecked())
        s.setAsrLanguage(m_asrLang->text().trimmed());

    SegmentParams p;
    p.noiseThreshold = m_noise->value();
    p.minGapMs = m_minGap->value();
    p.minSentenceMs = m_minSentence->value();
    p.allowedNoiseMs = m_allowedNoise->value();
    p.removeSilence = m_removeSilence->isChecked();
    s.setSegmentParams(p);

    s.setRepeatCount(m_repeatCount->value());
    s.setRepeatGapMs(m_repeatGap->value());
    s.setDictationIgnorePunctuation(m_ignorePunct->isChecked());
    s.setDictationIgnoreCase(m_ignoreCase->isChecked());
    s.setResumeEnabled(m_resumePlay->isChecked());
    s.setStudyLanguage(m_studyLang->currentData().toString());
    s.setSourceLang(m_sourceLang->currentData().toString());
    s.setTargetLang(m_targetLang->text().trimmed());
    s.setDictHoverLookupEnabled(m_hoverLookup->isChecked());
    s.setDictHoverDelayMs(m_hoverDelay->value());
    s.setRetellParagraphSize(m_retellParagraphSize->value());
    s.setRetellRecordEnabled(m_retellRecord->isChecked());

    // 快捷键
    detectConflicts();
    for (int r = 0; r < m_keysTable->rowCount(); ++r) {
        const QString id = m_keysTable->cellWidget(r, 1)
                               ? m_keysTable->cellWidget(r, 1)->property("actionId").toString()
                               : QString();
        if (id.isEmpty())
            continue;
        if (auto *edit = qobject_cast<QKeySequenceEdit *>(m_keysTable->cellWidget(r, 1)))
            s.setKeySequence(id, edit->keySequence());
    }
}

void SettingsDialog::detectConflicts()
{
    QMap<QString, QString> seqToAction;
    for (int r = 0; r < m_keysTable->rowCount(); ++r) {
        auto *edit = qobject_cast<QKeySequenceEdit *>(m_keysTable->cellWidget(r, 1));
        if (!edit)
            continue;
        const QString seq = edit->keySequence().toString(QKeySequence::PortableText);
        const QString id = edit->property("actionId").toString();
        if (seq.isEmpty()) {
            edit->setStyleSheet(QString());
            continue;
        }
        if (seqToAction.contains(seq) && seqToAction[seq] != id) {
            edit->setStyleSheet(QStringLiteral("background:#7a2a2a;")); // 冲突提示
        } else {
            edit->setStyleSheet(QString());
            seqToAction.insert(seq, id);
        }
    }
}

} // namespace adoloop
