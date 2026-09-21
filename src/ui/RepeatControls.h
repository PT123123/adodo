#pragma once

#include "../core/Types.h"

#include <QWidget>

class QPushButton;
class QSpinBox;

namespace adoloop {

// 复读操控条：上一句/下一句/本句复读/全文方案/A-B 标记；重复次数与间隔设置。
class RepeatControls : public QWidget {
    Q_OBJECT
public:
    explicit RepeatControls(QWidget *parent = nullptr);

    int repeatCount() const;
    int gapMs() const;
    bool fullScriptMode() const;

signals:
    void prevSentenceRequested();
    void nextSentenceRequested();
    void repeatSentenceRequested();
    void fullScriptToggled(bool on);
    void repeatConfigChanged(int count, int gapMs);

private:
    QPushButton *m_prev = nullptr;
    QPushButton *m_next = nullptr;
    QPushButton *m_repeat = nullptr;
    QPushButton *m_fullScript = nullptr;
    QSpinBox *m_countSpin = nullptr;
    QSpinBox *m_gapSpin = nullptr;
};

} // namespace adoloop
