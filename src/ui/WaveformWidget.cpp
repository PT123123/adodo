#include "WaveformWidget.h"

#include <QMouseEvent>
#include <QPainter>

namespace adoloop {

namespace {
const QColor kBg(24, 26, 32);
const QColor kPeak(90, 140, 220);
const QColor kSentenceBg(255, 255, 255, 26);
const QColor kSentenceCurrent(255, 200, 60, 90);
const QColor kCursor(240, 90, 90);
const QColor kAbSel(80, 200, 120, 70);
}

WaveformWidget::WaveformWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(96);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(true);
}

void WaveformWidget::setWaveform(const Waveform *wave)
{
    m_wave = wave;
    update();
}

void WaveformWidget::setSentences(const QVector<Sentence> *sentences)
{
    m_sentences = sentences;
    update();
}

void WaveformWidget::setPosition(Ms pos)
{
    m_pos = pos;
    update();
}

void WaveformWidget::setCurrentSentence(int index)
{
    m_currentSentence = index;
    update();
}

void WaveformWidget::clear()
{
    m_wave = nullptr;
    m_sentences = nullptr;
    m_currentSentence = -1;
    update();
}

Ms WaveformWidget::msAtX(int x) const
{
    if (!m_wave || width() <= 0)
        return 0;
    const qint64 totalMs = m_wave->totalDurationMs();
    const double frac = qBound(0.0, double(x) / double(width()), 1.0);
    return Ms(qRound64(frac * totalMs));
}

void WaveformWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), kBg);

    if (!m_wave || !m_wave->isReady()) {
        p.setPen(QColor(150, 150, 150));
        p.drawText(rect(), Qt::AlignCenter,
                   QStringLiteral("波形（需要 ffmpeg 解码；打开媒体后自动生成）"));
        return;
    }

    const qint64 totalMs = m_wave->totalDurationMs();
    if (totalMs <= 0)
        return;
    const int w = width();
    const int h = height();
    const int mid = h / 2;
    const int blockCount = m_wave->peakCount();
    const Ms blockMs = Waveform::kBlockMs;

    // 句段色块
    if (m_sentences) {
        for (int i = 0; i < m_sentences->size(); ++i) {
            const Sentence &s = m_sentences->at(i);
            const int x1 = int(double(s.start) / totalMs * w);
            const int x2 = int(double(s.end) / totalMs * w);
            p.fillRect(x1, 0, qMax(1, x2 - x1), h,
                       (i == m_currentSentence) ? kSentenceCurrent : kSentenceBg);
        }
    }

    // A-B 区间
    p.fillRect(0, 0, w, h, QColor(255, 255, 255, 6));

    // 峰值柱：每像素列聚合多个块
    for (int x = 0; x < w; ++x) {
        const Ms startMs = Ms(qint64(x) * totalMs / w);
        const Ms endMs = Ms(qint64(x + 1) * totalMs / w);
        const int b0 = int(startMs / blockMs);
        const int b1 = qMin(blockCount - 1, int(endMs / blockMs));
        float mn = 1.0f, mx = -1.0f;
        for (int b = b0; b <= b1; ++b) {
            mn = qMin(mn, m_wave->minPeaks()[b]);
            mx = qMax(mx, m_wave->maxPeaks()[b]);
        }
        if (b1 < b0)
            continue;
        const int y1 = int(mid - mx * (mid - 2));
        const int y2 = int(mid - mn * (mid - 2));
        p.setPen(kPeak);
        p.drawLine(x, y1, x, y2);
    }

    // 中线
    p.setPen(QColor(60, 60, 60));
    p.drawLine(0, mid, w, mid);

    // 游标
    const int cx = int(double(m_pos) / totalMs * w);
    p.setPen(kCursor);
    p.drawLine(cx, 0, cx, h);
}

void WaveformWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressX = event->pos().x();
        m_dragX = m_pressX;
        m_selectingAb = event->modifiers().testFlag(Qt::ShiftModifier);
        if (!m_selectingAb) {
            emit seekRequested(msAtX(m_pressX));
        }
        update();
    }
}

void WaveformWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_pressX >= 0 && m_selectingAb) {
        m_dragX = event->pos().x();
        update();
    } else if (m_pressX >= 0 && !m_selectingAb) {
        // 拖动 seek
        emit seekRequested(msAtX(event->pos().x()));
    }
}

void WaveformWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_pressX < 0)
        return;
    if (m_selectingAb) {
        const int x1 = qMin(m_pressX, m_dragX);
        const int x2 = qMax(m_pressX, m_dragX);
        if (x2 - x1 > 4) {
            emit abRangeSelected(msAtX(x1), msAtX(x2));
        }
    }
    m_pressX = -1;
    m_dragX = -1;
    m_selectingAb = false;
    update();
}

} // namespace adoloop
