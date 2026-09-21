#include "DictionaryPopup.h"

#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

namespace adoloop {

DictionaryPopup::DictionaryPopup(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setStyleSheet(QStringLiteral(
        "DictionaryPopup { background:#2b2d35; border:1px solid #444; border-radius:8px; }"
        "QLabel#title { color:#ffd75e; font-weight:bold; font-size:15px; }"
        "QLabel#body { color:#ddd; font-size:13px; }"
        "QPushButton { background:#3a3d4a; color:#eee; border:none; border-radius:4px; padding:3px 10px; }"
        "QPushButton:hover { background:#4a4e5e; }"));

    m_lay = new QVBoxLayout(this);
    m_lay->setContentsMargins(10, 8, 10, 8);
    m_lay->setSpacing(6);

    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("title"));
    m_title->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_body = new QLabel(this);
    m_body->setObjectName(QStringLiteral("body"));
    m_body->setWordWrap(true);
    m_body->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *row = new QHBoxLayout();
    m_addBtn = new QPushButton(QStringLiteral("加入生词本"), this);
    m_pronounceBtn = new QPushButton(QStringLiteral("朗读"), this);
    m_retryBtn = new QPushButton(QStringLiteral("重试"), this);
    m_closeBtn = new QPushButton(QStringLiteral("关闭"), this);
    row->addWidget(m_addBtn);
    row->addWidget(m_pronounceBtn);
    row->addWidget(m_retryBtn);
    row->addStretch(1);
    row->addWidget(m_closeBtn);

    m_lay->addWidget(m_title);
    m_lay->addWidget(m_body);
    m_lay->addLayout(row);

    connect(m_addBtn, &QPushButton::clicked, this,
            [this]() { emit addToVocabularyRequested(m_word); });
    connect(m_pronounceBtn, &QPushButton::clicked, this,
            [this]() { emit pronounceRequested(m_word, m_audioUrl); });
    connect(m_retryBtn, &QPushButton::clicked, this,
            [this]() { emit lookupRetry(m_word); });
    connect(m_closeBtn, &QPushButton::clicked, this, &DictionaryPopup::closePopup);
}

QString DictionaryPopup::buildTitle(const DictEntry &entry)
{
    QString title = entry.word;
    if (entry.lang == Lang::Japanese) {
        // 日语：表记 [假名] 声调（如「学生 [がくせい] ②」）
        if (!entry.reading.isEmpty() && entry.reading != entry.word)
            title += QStringLiteral("  [%1]").arg(entry.reading);
        if (!entry.accent.isEmpty())
            title += QStringLiteral("  %1").arg(entry.accent);
        return title;
    }
    // 英语：音标（与 M10 前一致）
    if (!entry.phoneticUs.isEmpty() || !entry.phoneticUk.isEmpty()) {
        title += QStringLiteral("  /%1/ /%2/").arg(entry.phoneticUs, entry.phoneticUk);
    }
    return title;
}

void DictionaryPopup::showEntry(const DictEntry &entry, const QPoint &globalPos)
{
    m_word = entry.word;
    m_audioUrl = entry.audioUrl;
    // 朗读按钮：词典未给音频时禁用（而不是点了没反应）
    m_pronounceBtn->setEnabled(!m_audioUrl.isEmpty());
    m_pronounceBtn->setToolTip(m_audioUrl.isEmpty()
                                   ? QStringLiteral("词典未返回发音音频")
                                   : m_audioUrl);

    QString body = entry.definitions.join(QStringLiteral("\n"));
    if (!entry.examples.isEmpty()) {
        body += QStringLiteral("\n\n── 例句 ──\n") + entry.examples.mid(0, 3).join(QStringLiteral("\n"));
    }
    m_retryBtn->setVisible(false);
    rebuild(buildTitle(entry), body, globalPos);
}

void DictionaryPopup::showLoading(const QString &word, const QPoint &globalPos)
{
    m_word = word;
    m_audioUrl.clear();
    m_pronounceBtn->setEnabled(false);
    m_retryBtn->setVisible(false);
    rebuild(word, QStringLiteral("查词中…"), globalPos);
}

void DictionaryPopup::showError(const QString &word, const QString &error, const QPoint &globalPos)
{
    m_word = word;
    m_audioUrl.clear();
    m_pronounceBtn->setEnabled(false);
    m_retryBtn->setVisible(true);
    rebuild(word, QStringLiteral("查询失败：%1").arg(error), globalPos);
}

void DictionaryPopup::closePopup()
{
    hide();
}

void DictionaryPopup::rebuild(const QString &title, const QString &body, const QPoint &globalPos)
{
    m_title->setText(title);
    m_body->setText(body);
    adjustSize();
    setMaximumWidth(420);
    // 限制在屏幕内
    const QRect scr = screen() ? screen()->availableGeometry() : QRect(0, 0, 1920, 1080);
    QPoint pos = globalPos;
    if (pos.x() + width() > scr.right())
        pos.setX(scr.right() - width());
    if (pos.y() + height() > scr.bottom())
        pos.setY(qMax(scr.top(), globalPos.y() - height()));
    move(pos);
    show();
    raise();
}

void DictionaryPopup::mousePressEvent(QMouseEvent *event)
{
    // 点击弹窗外即关闭
    QWidget::mousePressEvent(event);
}

} // namespace adoloop
