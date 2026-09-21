#include "SubtitleOverlay.h"
#include "../core/ReadingStore.h"
#include "../core/Sentence.h"
#include "../core/SubtitleModel.h"
#include "../core/Tokenizer.h"

#include <QMouseEvent>
#include <QPainter>
#include <QTextLayout>
#include <QTextLine>
#include <QTextOption>
#include <QTimer>

namespace adoloop {

namespace {
const QColor kText(255, 255, 255);
const QColor kTranslation(200, 200, 200);
const QColor kHighlight(255, 210, 60);
const QColor kHoverFill(255, 210, 60, 95);
const QColor kCurrentFill(120, 200, 255, 95);
const QColor kShadow(0, 0, 0, 160);
constexpr int kMargin = 24;
} // namespace

SubtitleOverlay::SubtitleOverlay(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true); // 悬停取词需要

    m_hoverTimer = new QTimer(this);
    m_hoverTimer->setSingleShot(true);
    connect(m_hoverTimer, &QTimer::timeout, this, [this]() {
        if (m_hoverIndex < 0 || m_hoverIndex >= m_boxes.size())
            return;
        m_hoverArmed = m_hoverIndex;
        emit wordHovered(m_boxes.at(m_hoverIndex).word, m_currentSentence);
    });
}

void SubtitleOverlay::setSubtitleModel(SubtitleModel *model)
{
    m_model = model;
    invalidateLayout();
    update();
}

void SubtitleOverlay::setCurrentSentence(int index)
{
    if (m_currentSentence != index) {
        m_currentSentence = index;
        m_currentWord.clear();
        m_hoverIndex = -1;
        m_hoverArmed = -1;
        if (m_hoverTimer)
            m_hoverTimer->stop();
        invalidateLayout();
        update();
    }
}

void SubtitleOverlay::setCurrentWord(const QString &word)
{
    m_currentWord = word;
    update();
}

void SubtitleOverlay::setShowTranslation(bool show)
{
    m_showTranslation = show;
    invalidateLayout();
    update();
}

void SubtitleOverlay::setConcealed(bool concealed)
{
    if (m_concealed == concealed)
        return;
    m_concealed = concealed;
    // 隐藏时清掉高亮与命中表：既看不到原文，也没法用查词弹窗「偷看」
    clearHover(true);
    m_currentWord.clear();
    invalidateLayout();
    update();
}

void SubtitleOverlay::setHoverDelayMs(int delayMs)
{
    m_hoverDelayMs = qBound(0, delayMs, 5000);
}

void SubtitleOverlay::setHoverLookupEnabled(bool enabled)
{
    m_hoverLookup = enabled;
    if (!enabled)
        clearHover(true);
}

void SubtitleOverlay::clear()
{
    m_model = nullptr;
    m_currentSentence = -1;
    m_currentWord.clear();
    clearHover(false);
    invalidateLayout();
    update();
}

QFont SubtitleOverlay::subtitleFont() const
{
    QFont f = font();
    f.setPointSize(qMax(11, width() / 60));
    return f;
}

QFont SubtitleOverlay::translationFont() const
{
    QFont f = font();
    f.setPointSize(qMax(9, width() / 80));
    return f;
}

void SubtitleOverlay::invalidateLayout()
{
    m_layoutDirty = true;
}

QString SubtitleOverlay::currentText() const
{
    if (!m_model || m_currentSentence < 0 || m_currentSentence >= m_model->count())
        return {};
    return m_model->at(m_currentSentence).text;
}

// ---------- 词级命中：QTextLayout 排版 + 逐 token 像素区间 ----------

QVector<SubtitleOverlay::WordBox> SubtitleOverlay::buildHitMap(const QString &text,
                                                              const QVector<Token> &tokens,
                                                              const QFont &font, int lineWidth,
                                                              QVector<QRectF> *lineRects)
{
    QVector<WordBox> boxes;
    if (lineRects)
        lineRects->clear();
    if (text.isEmpty())
        return boxes;

    QTextLayout layout(text, font);
    QTextOption opt;
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere); // 中日文可在任意字间换行
    opt.setAlignment(Qt::AlignHCenter);
    layout.setTextOption(opt);

    layout.beginLayout();
    qreal y = 0.0;
    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(qMax(1, lineWidth));
        line.setPosition(QPointF(0, y));
        if (lineRects)
            *lineRects << QRectF(line.x(), y, line.naturalTextWidth(), line.height());
        y += line.height();
    }
    layout.endLayout();

    const int lineCount = layout.lineCount();
    for (const Token &t : tokens) {
        if (t.begin < 0 || t.length <= 0)
            continue;
        WordBox box;
        box.word = t.surface;
        box.begin = t.begin;
        box.length = t.length;
        const int tEnd = t.begin + t.length;
        for (int li = 0; li < lineCount; ++li) {
            const QTextLine line = layout.lineAt(li);
            const int ls = line.textStart();
            const int le = ls + line.textLength();
            const int s = qMax(t.begin, ls);
            const int e = qMin(tEnd, le);
            if (s >= e)
                continue;
            // cursorToX 相对行首；行在块内的横向偏移由 line.x() 给出（居中时非 0）
            const qreal x1 = line.x() + line.cursorToX(s);
            const qreal x2 = line.x() + line.cursorToX(e);
            box.rects << QRectF(x1, line.y(), qMax<qreal>(1.0, x2 - x1), line.height());
        }
        if (!box.rects.isEmpty())
            boxes << box;
    }
    return boxes;
}

void SubtitleOverlay::ensureLayout() const
{
    if (!m_layoutDirty)
        return;
    m_layoutDirty = false;
    m_boxes.clear();
    m_lineRects.clear();
    delete m_layout;
    m_layout = nullptr;

    const QString text = currentText();
    if (text.isEmpty())
        return;
    // 复述隐藏期间不排版：既不绘制原文，也让词级命中表为空
    // （否则可以用悬停/点击查词「偷看」隐藏的原文）
    if (m_concealed)
        return;

    const QFont f = subtitleFont();
    const int lineWidth = qMax(1, width() - kMargin * 2);

    QTextLayout *layout = new QTextLayout(text, f);
    QTextOption opt;
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    opt.setAlignment(Qt::AlignHCenter);
    layout->setTextOption(opt);

    layout->beginLayout();
    qreal y = 0.0;
    QVector<QRectF> lines;
    while (true) {
        QTextLine line = layout->createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(lineWidth);
        line.setPosition(QPointF(0, y));
        lines << QRectF(line.x(), y, line.naturalTextWidth(), line.height());
        y += line.height();
    }
    layout->endLayout();

    m_layout = layout;
    m_lineRects = lines;

    // 命中词：优先句子的分词结果（外部引擎档带读音/原形），否则按语言现场分词
    QVector<Token> toks;
    if (m_model && m_currentSentence >= 0 && m_currentSentence < m_model->count())
        toks = m_model->at(m_currentSentence).lexTokens();
    m_boxes = buildHitMap(text, toks, f, lineWidth, nullptr);

    // 文本块垂直居中于上半区
    const qreal blockH = y;
    const qreal regionH = qMax<qreal>(40.0, (height() - kMargin * 2) * 0.6);
    const qreal top = kMargin + qMax<qreal>(0.0, (regionH - blockH) / 2.0);
    m_blockOrigin = QPointF(kMargin, top);
}

int SubtitleOverlay::hitIndexAt(const QPoint &pos) const
{
    ensureLayout();
    const QPointF local = QPointF(pos) - m_blockOrigin;
    for (int i = 0; i < m_boxes.size(); ++i) {
        for (const QRectF &r : m_boxes.at(i).rects) {
            // 行高比字形大：横向留 1px 容差，纵向用整行高度
            const QRectF hit = r.adjusted(-1.0, 0.0, 1.0, 0.0);
            if (hit.contains(local))
                return i;
        }
    }
    return -1;
}

void SubtitleOverlay::clearHover(bool notify)
{
    const bool had = m_hoverIndex >= 0;
    m_hoverIndex = -1;
    m_hoverArmed = -1;
    if (m_hoverTimer)
        m_hoverTimer->stop();
    if (had) {
        unsetCursor();
        update();
        if (notify)
            emit hoverCleared();
    }
}

// ---------- 绘制 ----------

void SubtitleOverlay::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(0, 0, 0, 90));

    if (!m_model || m_currentSentence < 0 || m_currentSentence >= m_model->count()) {
        p.setPen(QColor(180, 180, 180, 160));
        p.drawText(rect().adjusted(0, 0, 0, -40), Qt::AlignCenter,
                   QStringLiteral("加载媒体后将显示双语字幕 · 悬停或点击词可查字典"));
        return;
    }

    ensureLayout();
    const Sentence &s = m_model->at(m_currentSentence);

    // 段落复述：隐藏原文（不绘制原文/译文，也不给词级命中），只显示占位提示
    if (m_concealed) {
        p.setFont(subtitleFont());
        p.setPen(QColor(0, 0, 0, 170));
        p.drawText(QRect(0, height() / 3 + 2, width(), 40), Qt::AlignHCenter | Qt::AlignVCenter,
                   QStringLiteral("◇ 原文已隐藏 · 复述练习中"));
        p.setPen(QColor(255, 210, 60, 230));
        p.drawText(QRect(0, height() / 3, width(), 40), Qt::AlignHCenter | Qt::AlignVCenter,
                   QStringLiteral("◇ 原文已隐藏 · 复述练习中"));
        p.setFont(translationFont());
        p.setPen(QColor(180, 180, 180, 200));
        p.drawText(QRect(0, height() / 3 + 40, width(), 24), Qt::AlignHCenter | Qt::AlignVCenter,
                   QStringLiteral("共 %1 字 · 先听原音，再回忆复述（揭晓后显示）").arg(s.text.size()));
        return;
    }

    // 高亮：当前发音词（蓝）与悬停词（黄）先铺底色，再画文字
    auto fillBox = [&](int index, const QColor &color) {
        if (index < 0 || index >= m_boxes.size())
            return;
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        for (const QRectF &r : m_boxes.at(index).rects)
            p.drawRoundedRect(r.translated(m_blockOrigin), 3, 3);
        p.setBrush(Qt::NoBrush);
    };
    if (!m_currentWord.isEmpty()) {
        for (int i = 0; i < m_boxes.size(); ++i) {
            if (m_boxes.at(i).word == m_currentWord) {
                fillBox(i, kCurrentFill);
                break;
            }
        }
    }
    fillBox(m_hoverIndex, kHoverFill);

    // 原文：QTextLayout 逐行绘制（居中），与命中区间同源
    if (m_layout) {
        p.setPen(kShadow);
        m_layout->draw(&p, m_blockOrigin + QPointF(2, 2));
        p.setPen(kText);
        m_layout->draw(&p, m_blockOrigin);
    }

    // 译文：维持原有绘制方式（整块居中 + 自动换行）
    if (m_showTranslation && !s.translation.isEmpty()) {
        p.setFont(translationFont());
        const qreal belowBlock = m_lineRects.isEmpty() ? 60.0 : m_lineRects.last().bottom() + 14.0;
        const int transY = int(m_blockOrigin.y() + qMax<qreal>(30.0, belowBlock));
        const QRect tRect(kMargin, transY, width() - kMargin * 2,
                          qMax(24, height() - transY - kMargin));
        p.setPen(QColor(0, 0, 0, 160));
        p.drawText(tRect.translated(2, 2), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                   s.translation);
        p.setPen(kTranslation);
        p.drawText(tRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, s.translation);
    }

    // 悬停词名（原文块下方小字提示，便于确认命中的是哪个词）
    // M12：读音缓存里有假名时显示「漢字 [かな]」，不需要联网查词
    if (m_hoverIndex >= 0 && m_hoverIndex < m_boxes.size()) {
        p.setFont(translationFont());
        p.setPen(kHighlight);
        QString hint = m_boxes.at(m_hoverIndex).word;
        if (m_readings) {
            const QString r = m_readings->reading(hint);
            if (!r.isEmpty() && r != hint)
                hint = QStringLiteral("%1 [%2]").arg(hint, r);
        }
        const qreal y = m_blockOrigin.y() + (m_lineRects.isEmpty() ? 0.0 : m_lineRects.last().bottom());
        p.drawText(QRect(kMargin, int(y) + 2, width() - kMargin * 2, 20),
                   Qt::AlignHCenter | Qt::AlignTop, hint);
    }
}

// ---------- 交互 ----------

void SubtitleOverlay::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    if (m_concealed)
        return; // 隐藏期间不取词（避免用弹窗偷看原文）
    const int idx = hitIndexAt(event->pos());
    if (idx >= 0) {
        emit wordClicked(m_boxes.at(idx).word, m_currentSentence);
        return;
    }
    // 点击空白：清除高亮
    m_currentWord.clear();
    clearHover(true);
    update();
}

void SubtitleOverlay::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_model) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    if (m_concealed) {
        // 隐藏期间不命中任何词（命中表为空，这里再挡一次并清掉残留高亮）
        if (m_hoverIndex >= 0)
            clearHover(true);
        QWidget::mouseMoveEvent(event);
        return;
    }
    const int idx = hitIndexAt(event->pos());
    if (idx != m_hoverIndex) {
        m_hoverIndex = idx;
        if (idx < 0)
            m_hoverArmed = -1;
        if (idx >= 0)
            setCursor(Qt::PointingHandCursor);
        else
            unsetCursor();
        update();

        if (idx < 0) {
            if (m_hoverTimer)
                m_hoverTimer->stop();
            emit hoverCleared();
            return;
        }
    }
    // 悬停延时触发；「仅点击时查词」不启动定时器，同一个词只触发一次
    if (m_hoverLookup && m_hoverDelayMs > 0 && idx >= 0 && idx != m_hoverArmed
        && m_hoverTimer && !m_hoverTimer->isActive()) {
        m_hoverTimer->start(m_hoverDelayMs);
    }
    QWidget::mouseMoveEvent(event);
}

void SubtitleOverlay::leaveEvent(QEvent *event)
{
    clearHover(true);
    QWidget::leaveEvent(event);
}

void SubtitleOverlay::resizeEvent(QResizeEvent *event)
{
    invalidateLayout();
    QWidget::resizeEvent(event);
}

} // namespace adoloop
