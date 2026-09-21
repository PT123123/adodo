#pragma once

#include "../core/Types.h"

#include <QFont>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>
#include <QWidget>

class QTextLayout;
class QTimer;

namespace adoloop {

class SubtitleModel;
class ReadingStore;
struct Token;

// 字幕浮层：叠加在视频/音频区域，显示当前句双语。
//
// M11 起：原文行改用 QTextLayout 真正排版并记录每个分词的像素区间，
// 实现词级悬停/点击命中（此前是「点击即查句首词」的简化版）。
// 日语按分词结果命中（私は学生です → 悬停「学生」只查「学生」）；
// 译文行维持原有绘制方式。
//
// M12 起：支持**隐藏/揭晓**（段落复述时不得显示原文，否则等于抄答案）——
// setConcealed(true) 后原文与译文都不绘制、命中表失效（悬停/点击也拿不到词，
// 避免用查词弹窗「偷看」），只显示字数占位；揭晓后恢复。音频播放不受影响。
class SubtitleOverlay : public QWidget {
    Q_OBJECT
public:
    explicit SubtitleOverlay(QWidget *parent = nullptr);

    void setSubtitleModel(SubtitleModel *model);
    void setCurrentSentence(int index);
    void setCurrentWord(const QString &word); // 高亮当前发音词
    void setShowTranslation(bool show);
    void clear();

    // 隐藏/揭晓原文（段落复述用；M12）
    void setConcealed(bool concealed);
    bool concealed() const { return m_concealed; }

    // 读音缓存（M12，可空）：悬停提示里显示「漢字 [かな]」，无需联网
    void setReadingStore(const ReadingStore *store) { m_readings = store; }

    // 悬停配置（M11，由 Settings 注入）：delayMs<=0 或 enabled=false → 仅点击查词
    void setHoverDelayMs(int delayMs);
    void setHoverLookupEnabled(bool enabled);
    int hoverDelayMs() const { return m_hoverDelayMs; }
    bool hoverLookupEnabled() const { return m_hoverLookup; }

    // 一个词的命中区间（跨行时 rects 多于一个；坐标相对文本块左上角）
    struct WordBox {
        QString word;
        int begin = -1;
        int length = 0;
        QVector<QRectF> rects;
    };

    // 纯几何计算（无副作用，可离线单测）：按 QTextLayout 排版 text，
    // 给出 tokens 各词的像素区间；lineRects 回传每行块（供绘制与换行核对）。
    static QVector<WordBox> buildHitMap(const QString &text, const QVector<Token> &tokens,
                                        const QFont &font, int lineWidth,
                                        QVector<QRectF> *lineRects = nullptr);

signals:
    void wordClicked(const QString &word, int sentenceIndex);
    void wordHovered(const QString &word, int sentenceIndex); // 悬停延时到点（AppController 再去抖查词）
    void hoverCleared();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QFont subtitleFont() const;
    QFont translationFont() const;
    void invalidateLayout();
    void ensureLayout() const;                 // 惰性重排（绘制/命中前调用）
    int hitIndexAt(const QPoint &pos) const;   // -1 = 未命中任何词
    void clearHover(bool notify);
    QString currentText() const;

    SubtitleModel *m_model = nullptr;
    int m_currentSentence = -1;
    QString m_currentWord;
    bool m_showTranslation = true;
    bool m_concealed = false;              // 复述隐藏原文（M12）
    const ReadingStore *m_readings = nullptr; // 读音缓存（悬停提示用，M12）

    // 排版结果（ensureLayout 生成；mutable 以便 const 命中查询里惰性构建）
    mutable QTextLayout *m_layout = nullptr;
    mutable QVector<WordBox> m_boxes;
    mutable QVector<QRectF> m_lineRects;
    mutable QPointF m_blockOrigin; // 文本块左上角（widget 坐标）
    mutable bool m_layoutDirty = true;

    int m_hoverIndex = -1;
    int m_hoverArmed = -1; // 已触发过悬停的词索引（同一词不重复触发）
    QTimer *m_hoverTimer = nullptr;
    int m_hoverDelayMs = 450;
    bool m_hoverLookup = true;
};

} // namespace adoloop
