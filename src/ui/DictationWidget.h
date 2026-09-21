#pragma once

#include "../core/Types.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QGridLayout;

namespace adoloop {

class DictationSession;

// 听写界面：抠词槽位 + 输入框 + 实时反馈 + 汇报。
class DictationWidget : public QWidget {
    Q_OBJECT
public:
    explicit DictationWidget(QWidget *parent = nullptr);

    void attachSession(DictationSession *session);
    void detachSession();

    // 状态刷新
    void refreshSlots(const QStringList &words, const QVector<bool> &filled);
    void setFeedback(bool ok); // 整句判定反馈
    void showReport(const QString &summary);

signals:
    void inputSubmitted(const QString &text);
    void revealRequested();
    void nextRequested();
    void beginRequested(DictationSession *session);
    void stopRequested();

private:
    void rebuildSlots();

    DictationSession *m_session = nullptr;
    QStringList m_words;
    QVector<bool> m_filled;
    QGridLayout *m_slotLay = nullptr;
    QWidget *m_slotBox = nullptr;
    QLineEdit *m_input = nullptr;
    QLabel *m_feedback = nullptr;
    QPushButton *m_beginBtn = nullptr;
    QPushButton *m_revealBtn = nullptr;
    QPushButton *m_nextBtn = nullptr;
    QPushButton *m_stopBtn = nullptr;
};

} // namespace adoloop
