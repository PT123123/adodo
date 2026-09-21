#include "SoundSearchDialog.h"
#include "../util/JapaneseInput.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace adoloop {

SoundSearchDialog::SoundSearchDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("听音查字"));
    setMinimumWidth(420);
    setStyleSheet(QStringLiteral(
        "QDialog { background:#2b2d35; }"
        "QLabel { color:#ddd; }"
        "QLabel#preview { color:#ffd75e; }"
        "QLabel#status { color:#9aa; }"
        "QLineEdit { background:#1f2128; color:#eee; border:1px solid #444;"
        "            border-radius:4px; padding:4px 6px; font-size:14px; }"
        "QPushButton { background:#3a3d4a; color:#eee; border:none; border-radius:4px;"
        "              padding:5px 12px; }"
        "QPushButton:hover { background:#4a4e5e; }"
        "QPushButton:disabled { background:#2f3138; color:#777; }"));

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(12, 10, 12, 10);
    lay->setSpacing(8);

    auto *tip = new QLabel(
        QStringLiteral("输入 汉字 / 假名 / 罗马字（如 gakusei → がくせい），或点「录音」说出想查的词。"),
        this);
    tip->setWordWrap(true);
    lay->addWidget(tip);

    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(QStringLiteral("例：学生 / がくせい / gakusei"));
    connect(m_input, &QLineEdit::returnPressed, this, &SoundSearchDialog::submit);
    connect(m_input, &QLineEdit::textChanged, this, [this]() { updatePreview(); });
    lay->addWidget(m_input);

    m_preview = new QLabel(this);
    m_preview->setObjectName(QStringLiteral("preview"));
    m_preview->setMinimumHeight(18);
    lay->addWidget(m_preview);

    auto *row = new QHBoxLayout();
    m_lookupBtn = new QPushButton(QStringLiteral("查词"), this);
    m_recordBtn = new QPushButton(QStringLiteral("录音"), this);
    m_closeBtn = new QPushButton(QStringLiteral("关闭"), this);
    row->addWidget(m_lookupBtn);
    row->addWidget(m_recordBtn);
    row->addStretch(1);
    row->addWidget(m_closeBtn);
    lay->addLayout(row);

    m_status = new QLabel(QStringLiteral("ASR 未安装时可直接输入查询；录音需要麦克风与 Whisper 引擎。"), this);
    m_status->setObjectName(QStringLiteral("status"));
    m_status->setWordWrap(true);
    lay->addWidget(m_status);

    connect(m_lookupBtn, &QPushButton::clicked, this, &SoundSearchDialog::submit);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::close);
    connect(m_recordBtn, &QPushButton::clicked, this, [this]() {
        if (m_busy)
            return;
        m_recording = !m_recording;
        setRecording(m_recording);
        emit recordToggled(m_recording);
    });
}

void SoundSearchDialog::setStatus(const QString &text)
{
    m_status->setText(text);
}

void SoundSearchDialog::setRecognizedText(const QString &text)
{
    m_input->setText(text);
    updatePreview();
    m_input->setFocus();
    m_input->selectAll();
}

void SoundSearchDialog::setRecording(bool recording)
{
    m_recording = recording;
    m_recordBtn->setText(recording ? QStringLiteral("停止并识别") : QStringLiteral("录音"));
    m_lookupBtn->setEnabled(!recording && !m_busy);
}

void SoundSearchDialog::setBusy(bool busy)
{
    m_busy = busy;
    m_recordBtn->setEnabled(!busy);
    m_lookupBtn->setEnabled(!busy && !m_recording);
}

void SoundSearchDialog::focusInput()
{
    m_input->setFocus();
    m_input->selectAll();
}

void SoundSearchDialog::updatePreview()
{
    const QString raw = m_input->text().trimmed();
    if (raw.isEmpty()) {
        m_preview->clear();
        return;
    }
    if (japaneseinput::looksLikeRomaji(raw)) {
        const QString kana = japaneseinput::romajiToHiragana(raw);
        m_preview->setText(kana == raw ? QString() : QStringLiteral("→ %1").arg(kana));
        return;
    }
    m_preview->clear();
}

void SoundSearchDialog::submit()
{
    QString text = m_input->text().trimmed();
    if (text.isEmpty()) {
        setStatus(QStringLiteral("请输入要查的词，或先录音。"));
        return;
    }
    const bool romaji = japaneseinput::looksLikeRomaji(text);
    const QString query = japaneseinput::normalizeQuery(text);
    if (romaji && query != text) {
        m_input->setText(query); // 显示转换结果，便于确认
        setStatus(QStringLiteral("罗马字 → 假名：%1，正在查词…").arg(query));
    } else {
        setStatus(QStringLiteral("查词：%1 …").arg(query));
    }
    emit lookupRequested(query);
}

} // namespace adoloop
