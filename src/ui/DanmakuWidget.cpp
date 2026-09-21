#include "DanmakuWidget.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>

namespace adoloop {

namespace {
const double kSpeed = 60.0; // 像素/秒
const int kLaneHeight = 22;
const QColor kNewColor(255, 120, 80);
const QColor kReviewColor(120, 200, 255);
}

DanmakuWidget::DanmakuWidget(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setMouseTracking(true);
    m_timer.setInterval(33);
    connect(&m_timer, &QTimer::timeout, this, &DanmakuWidget::advance);
}

void DanmakuWidget::showDanmaku(const DanmakuController::Item &item)
{
    Flying f;
    f.word = item.word;
    f.display = item.display(); // M12：日语生词显示「漢字（かな）」
    if (f.display.isEmpty())
        f.display = item.word;
    f.sentence = item.sentence;
    f.level = item.level;
    f.isNew = item.isNew;
    f.color = item.isNew ? kNewColor : kReviewColor;

    QFont font = this->font();
    font.setBold(true);
    font.setPointSize(qMax(9, height() / 34));
    const QFontMetrics fm(font);
    f.width = fm.horizontalAdvance(f.display) + 20;

    // 选轨道：取进度最小的轨道
    int best = 0;
    for (int i = 1; i < m_laneCount; ++i) {
        if (m_laneProgress.value(i, 0) < m_laneProgress.value(best, 0))
            best = i;
    }
    f.lane = best;
    m_laneProgress[best] = m_laneProgress.value(best, 0) + f.width;
    f.x = double(width());

    m_items.append(f);
    if (!m_timer.isActive())
        m_timer.start();
    update();
}

void DanmakuWidget::clear()
{
    m_items.clear();
    m_laneProgress.clear();
    m_timer.stop();
    update();
}

void DanmakuWidget::setEnabled2(bool enabled)
{
    if (!enabled)
        clear();
    setVisible(enabled);
}

void DanmakuWidget::advance()
{
    const double dt = 0.033;
    for (int i = m_items.size() - 1; i >= 0; --i) {
        m_items[i].x -= kSpeed * dt;
        if (m_items[i].x + m_items[i].width < 0) {
            m_laneProgress[m_items[i].lane] = qMax<qint64>(0, m_laneProgress.value(m_items[i].lane) - m_items[i].width);
            m_items.removeAt(i);
        }
    }
    if (m_items.isEmpty())
        m_timer.stop();
    update();
}

void DanmakuWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int laneH = qMax(kLaneHeight, height() / qMax(1, m_laneCount));
    QFont font = this->font();
    font.setBold(true);
    font.setPointSize(qMax(9, height() / 34));
    p.setFont(font);

    for (const Flying &f : m_items) {
        const int y = f.lane * laneH + 4;
        p.setPen(QColor(0, 0, 0, 170));
        p.drawText(QPointF(f.x + 1, y + 1), f.display);
        p.setPen(f.color);
        p.drawText(QPointF(f.x, y), f.display);
    }
}

void DanmakuWidget::mousePressEvent(QMouseEvent *event)
{
    // 命中检测：取最上层（数组末尾）位于点击点的弹幕
    const QFontMetrics fm(font());
    for (int i = m_items.size() - 1; i >= 0; --i) {
        const Flying &f = m_items[i];
        const int laneH = qMax(kLaneHeight, height() / qMax(1, m_laneCount));
        const QRect r(int(f.x), f.lane * laneH, f.width, laneH);
        if (r.contains(event->pos())) {
            emit wordClicked(f.word);
            return;
        }
    }
}

} // namespace adoloop
