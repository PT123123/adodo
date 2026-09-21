#pragma once

#include "../core/Sentence.h"
#include "../core/Types.h"

#include <QObject>
#include <QString>
#include <QVector>

namespace adoloop {

// 段落复述 Retell（Echo-Loop 链路，docs/01 P0 第 10 条）：
//
//   听原文 → 隐藏文本(Concealed) → 复述 Retelling（可选录音）→ 揭晓对照 Revealed
//        → 自评 SelfRating → 下一段 → … → Finished
//
// 与 RepeatSession / ShadowingSession 同一约定（docs/08 第 8 节「状态机共约」）：
// **纯逻辑、不依赖 UI**——播放/录音命令经信号由 AppController 执行，
// 位置与录音数据由外部回灌；因此可离线单测。
//
// 范围（Scope）：当前句 / 意群（复用 Sentence::chunks()）/ 段落（连续 N 句，N 可配）/ 全文。
//
// 无麦克风可用：micAvailable=false 或用户关闭录音（recordAudio=false）时，
// 自动降级为「静默回忆 → 揭晓对照 → 自评」——Concealed 状态下直接 reveal()，
// 不进入 Retelling，不启动录音，不报错、不卡死（并有 notice 中文提示）。
class RetellSession : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, PlayingModel, Concealed, Retelling, Revealed, SelfRating, Finished };
    Q_ENUM(State)
    enum class Scope { Sentence, Chunk, Paragraph, FullScript };
    Q_ENUM(Scope)

    // 一个复述单元（句区间 + 时间区间 + 原文）
    struct Segment {
        int firstSentence = -1;
        int lastSentence = -1;
        Ms start = 0;
        Ms end = 0;
        QString text;        // 原文（隐藏/揭晓用）
        QString translation; // 译文（有则一并揭晓）
        int chunkIndex = -1; // 意群范围：第几个意群（0 基）
        int chunkCount = 0;  // 该句的意群总数
        bool wholeScript = false; // 全文范围（label 用）
        QString label() const; // 「第 3 句」/「意群 2/3」/「第 3–5 句」/「全文」
    };

    struct Config {
        Scope scope = Scope::Sentence;
        int paragraphSize = 3;    // 段落范围：连续 N 句（可配）
        bool recordAudio = true;  // 用户选择：复述时是否录音
        bool micAvailable = true; // 由 app 层探测注入（无设备 → 降级）
        int maxSegments = 0;      // 单次会话最多练多少段（0 = 不限）
    };

    // 一次练习记录（历史项 → retell.json）
    struct Item {
        QString media;         // 媒体标识（标题/路径）
        QString scope;         // "sentence" / "chunk" / "paragraph" / "full"
        int firstSentence = -1;
        int lastSentence = -1;
        int quality = -1;      // 自评 0..5（-1 = 未评）
        int score = -1;        // 包络比对得分 0..100（-1 = 未录音/未评分）
        QString detail;        // 比对细节或降级说明
        bool recorded = false;
        QString recordingPath;
        qint64 at = 0;         // epoch ms
        QString text;          // 原文（历史回看）
    };

    explicit RetellSession(QObject *parent = nullptr);

    void setSentences(const QVector<Sentence> *sentences); // 弱引用，外部持有
    void setConfig(const Config &c) { m_config = c; }
    const Config &config() const { return m_config; }
    void setMediaTitle(const QString &title) { m_mediaTitle = title; }

    // ---------- 会话控制 ----------
    // 按 config.scope 从 fromIndex 起切分复述段并开始
    void begin(int fromIndex);
    // 显式指定范围/参数（UI 下拉与单测用）
    void beginWith(int fromIndex, Scope scope, int paragraphSize = 3, bool recordAudio = true);
    void stop();        // 中途停止：停录音/取消播放/恢复文本/回 Idle
    void nextSegment(); // 跳过当前段（不计入历史）

    // ---------- 状态推进 ----------
    void tick(Ms pos);            // 播放位置回灌
    void onModelFinished();       // 模型音播完（tick 判定或引擎 eof 回灌）
    void onUserStartRetelling();  // 进入复述（有麦克风时开始录音）
    void onUserStopRetelling();   // 复述结束（停止录音，等数据回灌）
    void reveal();                // 揭晓对照
    void rate(int quality);       // 自评 0..5 → 记历史 → 下一段

    // 录音数据回灌（AppController 停止缓冲录音后调用；modelPcm 可为空）
    void onRecordingData(const QVector<float> &userPcm, int userSr,
                         const QVector<float> &modelPcm, int modelSr);
    // AppController 落盘后回填（记进历史项）
    void setRecordingPath(const QString &path) { m_pendingRecordingPath = path; }

    // ---------- 查询 ----------
    State state() const { return m_state; }
    Scope scope() const { return m_config.scope; }
    int currentIndex() const { return m_index; }
    int segmentCount() const { return m_segments.size(); }
    const QVector<Segment> &segments() const { return m_segments; }
    const Segment *currentSegment() const;
    bool concealed() const { return m_concealed; }
    bool micAvailable() const { return m_config.micAvailable; }
    // 本次练习是否会录音（有设备且用户未关闭）
    bool canRecord() const { return m_config.micAvailable && m_config.recordAudio; }
    const QVector<Item> &history() const { return m_history; }
    const Item *lastItem() const { return m_history.isEmpty() ? nullptr : &m_history.last(); }
    void clearHistory();

    // ---------- 纯函数（可离线单测） ----------
    // 把句子按范围切成复述段；越界 fromIndex 会被夹紧，空输入返回空
    static QVector<Segment> buildSegments(const QVector<Sentence> &sentences, Scope scope,
                                          int fromIndex, int paragraphSize = 3,
                                          int maxSegments = 0);
    static QString scopeCode(Scope s);          // "sentence"/"chunk"/"paragraph"/"full"
    static Scope scopeFromCode(const QString &code); // 未知/空 → Sentence
    static QString scopeDisplayName(Scope s);   // 「当前句」/「意群」/「段落」/「全文」
    static QString stateDisplayName(State s);   // 状态栏中文

    // ---------- 历史持久化（JSON 数组，上限 500 条） ----------
    bool loadHistoryFrom(const QString &path);
    bool saveHistoryTo(const QString &path) const;

signals:
    void stateChanged(RetellSession::State state);
    void segmentChanged(int index, const RetellSession::Segment &segment);
    void commandPlayRange(adoloop::Ms start, adoloop::Ms end);
    void commandPause();
    void commandStartBufferRecording();
    void commandStopBufferRecording();
    void commandCancelBufferRecording();
    void concealChanged(bool concealed); // 字幕浮层隐藏/揭晓
    void resultReady(const RetellSession::Item &item);
    void historyChanged();
    void notice(const QString &text);    // 中文提示（降级/录音/评分）
    void finished();

private:
    void setState(State s);
    void setConcealed(bool on);
    void startSegment();
    void advance();
    void appendHistory(const Item &it);
    Item buildItem() const;

    State m_state = State::Idle;
    Config m_config;
    const QVector<Sentence> *m_sentences = nullptr;
    QVector<Segment> m_segments;
    int m_index = -1;
    bool m_concealed = false;
    bool m_recorded = false;
    ShadowingScore m_score;
    int m_quality = -1;
    QString m_mediaTitle;
    QString m_pendingRecordingPath;
    QVector<Item> m_history;
};

} // namespace adoloop
