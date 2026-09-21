#pragma once

#include "../study/RetellSession.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTextEdit;

namespace adoloop {

// 段落复述界面（右侧页签，与「字幕 / 听写 / 造句」并列）。
// 只负责展示与收集用户操作：全部状态判断与流转都在 study/RetellSession（纯逻辑）里，
// 命令经信号交给 AppController 执行（docs/08 第 8 节约定）。
class RetellWidget : public QWidget {
    Q_OBJECT
public:
    explicit RetellWidget(QWidget *parent = nullptr);

    void attachSession(RetellSession *session);
    RetellSession *session() const { return m_session; }

    // 按会话状态刷新按钮可用性与文本区（隐藏/揭晓）
    void refresh();

    // 参数（由 AppController 与设置同步）
    RetellSession::Scope scope() const;
    void setScope(RetellSession::Scope scope);
    int paragraphSize() const;
    void setParagraphSize(int n);
    bool recordEnabled() const;
    void setRecordEnabled(bool on);

    // 提示行（降级/录音/评分等中文提示）
    void setHint(const QString &text);

    // 练习历史
    void showHistory(const QStringList &items, const QStringList &tips);

signals:
    void startRequested();
    void stopRequested();
    void startRetellingRequested(); // 开始复述（录音）
    void stopRetellingRequested();  // 说完了
    void revealRequested();         // 揭晓对照
    void nextRequested();           // 下一段 / 跳过
    void rateRequested(int quality); // 自评 0..5
    void optionsChanged();           // 范围/段落句数/录音开关变化（AppController 落设置）

private:
    void buildUi();
    void onScopeChanged();

    RetellSession *m_session = nullptr;
    QComboBox *m_scopeBox = nullptr;
    QSpinBox *m_paragraphSpin = nullptr;
    QCheckBox *m_recordBox = nullptr;
    QLabel *m_stateLabel = nullptr;
    QLabel *m_posLabel = nullptr;
    QLabel *m_hintLabel = nullptr;
    QTextEdit *m_text = nullptr;
    QPushButton *m_startBtn = nullptr;
    QPushButton *m_stopBtn = nullptr;
    QPushButton *m_retellBtn = nullptr;
    QPushButton *m_doneBtn = nullptr;
    QPushButton *m_revealBtn = nullptr;
    QPushButton *m_nextBtn = nullptr;
    QComboBox *m_rateBox = nullptr;
    QPushButton *m_rateBtn = nullptr;
    QListWidget *m_history = nullptr;
};

} // namespace adoloop
