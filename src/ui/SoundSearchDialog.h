#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;

namespace adoloop {

// 听音查字（M11）：录音 → ASR（日语）→ 查词；也可直接输入
// 汉字 / 假名 / 罗马字（内置罗马字→假名转换）查词。
// 本类只负责界面与信号，录音/识别/查词由 AppController 驱动。
class SoundSearchDialog : public QDialog {
    Q_OBJECT
public:
    explicit SoundSearchDialog(QWidget *parent = nullptr);

    void setStatus(const QString &text);      // 状态提示（识别中/失败原因等）
    void setRecognizedText(const QString &text); // ASR 结果回填到输入框
    void setRecording(bool recording);         // 录音按钮状态
    void setBusy(bool busy);                   // 识别中禁用按钮
    void focusInput();

signals:
    void lookupRequested(const QString &word);   // 已做罗马字→假名归一
    void recordToggled(bool start);              // true=开始录音，false=停止并识别

private:
    void submit();
    void updatePreview();

    QLineEdit *m_input = nullptr;
    QPushButton *m_lookupBtn = nullptr;
    QPushButton *m_recordBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;
    QLabel *m_preview = nullptr;
    QLabel *m_status = nullptr;
    bool m_recording = false;
    bool m_busy = false;
};

} // namespace adoloop
