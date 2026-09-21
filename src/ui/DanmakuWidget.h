#pragma once

#include "../study/DanmakuController.h"

#include <QTimer>
#include <QWidget>

namespace adoloop {

// 弹幕浮层：生词弹幕从右向左飘过，多轨道；点击弹幕查词。
class DanmakuWidget : public QWidget {
    Q_OBJECT
public:
    explicit DanmakuWidget(QWidget *parent = nullptr);

    void showDanmaku(const DanmakuController::Item &item);
    void clear();
    void setEnabled2(bool enabled); // 避免与 QWidget::setEnabled 歧义

signals:
    void wordClicked(const QString &word);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    struct Flying {
        QString word;      // 生词（点击查词用）
        QString display;   // 显示文本（M12：有读音时为「漢字（かな）」）
        QString sentence;
        int level = 1;
        bool isNew = true;
        double x = 0.0;
        int lane = 0;
        int width = 0;
        QColor color;
    };

    void advance();

    QVector<Flying> m_items;
    QTimer m_timer;
    int m_laneCount = 4;
    QVector<qint64> m_laneProgress; // 每轨道已发射像素（防碰撞简化）
};

} // namespace adoloop
