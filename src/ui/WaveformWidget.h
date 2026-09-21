#pragma once

#include "../core/Sentence.h"
#include "../core/Types.h"
#include "../core/Waveform.h"

#include <QWidget>

namespace adoloop {

// 波形控件：整轨峰值 + 句段色块 + 播放游标 + A-B 区间框选。
// - 左键点击/拖动：seek
// - Shift+左键拖动：选择 A-B 区间
// - 句段高亮当前句（由外部 setCurrentSentence 驱动）
class WaveformWidget : public QWidget {
    Q_OBJECT
public:
    explicit WaveformWidget(QWidget *parent = nullptr);

    void setWaveform(const Waveform *wave); // 外部持有
    void setSentences(const QVector<Sentence> *sentences);
    void setPosition(Ms pos);
    void setCurrentSentence(int index);
    void clear();

signals:
    void seekRequested(adoloop::Ms pos);
    void abRangeSelected(adoloop::Ms a, adoloop::Ms b);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    Ms msAtX(int x) const;

    const Waveform *m_wave = nullptr;
    const QVector<Sentence> *m_sentences = nullptr;
    Ms m_pos = 0;
    int m_currentSentence = -1;
    bool m_selectingAb = false;
    int m_pressX = -1;
    int m_dragX = -1;
    bool m_abEnabled = false;
};

} // namespace adoloop
