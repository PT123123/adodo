#include "RetellSession.h"
#include "../core/AudioAnalysis.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace adoloop {

namespace {
// 历史上限（防脏数据无限增长；与 PlaylistModel/BookmarkModel 同一风格）
constexpr int kMaxHistory = 500;

// 意群范围的段文本：取落在该时间区间内的词重组（日语不插空格）
QString chunkTextOf(const Sentence &s, Ms start, Ms end)
{
    const QVector<WordToken> ws = s.estimatedWords();
    QStringList parts;
    for (const WordToken &w : ws) {
        if (w.start < start || w.start >= end)
            continue;
        if (!w.word.trimmed().isEmpty())
            parts << w.word;
    }
    if (parts.isEmpty())
        return s.text; // 退化：拿不到词区间时用整句（宁可多给也不能空）
    return parts.join(s.japanese() ? QString() : QStringLiteral(" "));
}

// 一个意群切片（时间区间 + 文本）
struct ChunkSlice {
    TimeRange range;
    QString text;
};

// 意群切片：**优先复用 Sentence::chunks()**（词级时间戳/停顿时长）。
// 但内置日语分词会剥掉标点，chunks() 常常退化成「整句 1 段」——此时按文本标点
// 做按时长比例的二次切分（日语字幕里「、」「。」本身就是意群边界），
// 保证「意群」范围确实比「当前句」细。
QVector<ChunkSlice> chunkSlicesOf(const Sentence &s)
{
    QVector<ChunkSlice> out;
    const QVector<TimeRange> ranges = s.chunks();
    if (ranges.size() >= 2) {
        for (const TimeRange &r : ranges)
            out << ChunkSlice{r, chunkTextOf(s, r.start, r.end)};
        return out;
    }

    // 按标点切分（标点留在前一片段尾部）
    static const QString kPunct = QStringLiteral("、。！？，；：…‥「」『』（）,.!?;:");
    QStringList parts;
    QString cur;
    for (const QChar &c : s.text) {
        cur.append(c);
        if (kPunct.contains(c)) {
            parts << cur;
            cur.clear();
        }
    }
    if (!cur.isEmpty())
        parts << cur;
    if (parts.size() < 2) {
        out << ChunkSlice{TimeRange{s.start, s.end}, s.text}; // 无法再切：整句一段
        return out;
    }

    int total = 0;
    for (const QString &p : parts)
        total += qMax(1, p.size());
    const Ms dur = qMax<Ms>(1, s.end - s.start);
    Ms cursor = s.start;
    for (int i = 0; i < parts.size(); ++i) {
        const Ms len = (i == parts.size() - 1)
            ? qMax<Ms>(1, s.end - cursor)
            : Ms(dur) * Ms(qMax(1, parts.at(i).size())) / Ms(total);
        out << ChunkSlice{TimeRange{cursor, cursor + len}, parts.at(i).trimmed()};
        cursor += len;
    }
    return out;
}
} // namespace

QString RetellSession::Segment::label() const
{
    if (wholeScript)
        return QStringLiteral("全文（第 %1–%2 句）").arg(firstSentence + 1).arg(lastSentence + 1);
    if (chunkCount > 0)
        return QStringLiteral("第 %1 句 · 意群 %2/%3")
            .arg(firstSentence + 1)
            .arg(chunkIndex + 1)
            .arg(chunkCount);
    if (firstSentence == lastSentence)
        return QStringLiteral("第 %1 句").arg(firstSentence + 1);
    return QStringLiteral("第 %1–%2 句").arg(firstSentence + 1).arg(lastSentence + 1);
}

RetellSession::RetellSession(QObject *parent)
    : QObject(parent)
{
}

void RetellSession::setSentences(const QVector<Sentence> *sentences)
{
    m_sentences = sentences;
}

QString RetellSession::scopeCode(Scope s)
{
    switch (s) {
    case Scope::Sentence:
        return QStringLiteral("sentence");
    case Scope::Chunk:
        return QStringLiteral("chunk");
    case Scope::Paragraph:
        return QStringLiteral("paragraph");
    case Scope::FullScript:
        return QStringLiteral("full");
    }
    return QStringLiteral("sentence");
}

QString RetellSession::scopeDisplayName(Scope s)
{
    switch (s) {
    case Scope::Sentence:
        return QStringLiteral("当前句");
    case Scope::Chunk:
        return QStringLiteral("意群");
    case Scope::Paragraph:
        return QStringLiteral("段落");
    case Scope::FullScript:
        return QStringLiteral("全文");
    }
    return QStringLiteral("当前句");
}

RetellSession::Scope RetellSession::scopeFromCode(const QString &code)
{
    const QString c = code.trimmed().toLower();
    if (c == QLatin1String("chunk"))
        return Scope::Chunk;
    if (c == QLatin1String("paragraph"))
        return Scope::Paragraph;
    if (c == QLatin1String("full"))
        return Scope::FullScript;
    return Scope::Sentence;
}

QString RetellSession::stateDisplayName(State s)
{
    switch (s) {
    case State::Idle:
        return QStringLiteral("空闲");
    case State::PlayingModel:
        return QStringLiteral("播放原音（原文已隐藏）");
    case State::Concealed:
        return QStringLiteral("静默回忆");
    case State::Retelling:
        return QStringLiteral("复述录音中");
    case State::Revealed:
        return QStringLiteral("揭晓对照（请自评）");
    case State::SelfRating:
        return QStringLiteral("记录自评");
    case State::Finished:
        return QStringLiteral("复述练习完成");
    }
    return QStringLiteral("空闲");
}

// ---------- 分段（纯函数） ----------

QVector<RetellSession::Segment> RetellSession::buildSegments(const QVector<Sentence> &sentences,
                                                             Scope scope, int fromIndex,
                                                             int paragraphSize, int maxSegments)
{
    QVector<Segment> out;
    if (sentences.isEmpty())
        return out;

    const int last = sentences.size() - 1;
    const int from = qBound(0, fromIndex, last);
    const int n = qMax(1, paragraphSize);

    auto makeSpan = [&](int a, int b) {
        Segment seg;
        seg.firstSentence = a;
        seg.lastSentence = b;
        seg.start = sentences.at(a).start;
        seg.end = sentences.at(b).end;
        QStringList texts;
        for (int i = a; i <= b; ++i) {
            if (!sentences.at(i).text.trimmed().isEmpty())
                texts << sentences.at(i).text.trimmed();
            if (seg.translation.isEmpty() && !sentences.at(i).translation.trimmed().isEmpty())
                seg.translation = sentences.at(i).translation.trimmed();
        }
        seg.text = texts.join(sentences.at(a).japanese() ? QString() : QStringLiteral(" "));
        return seg;
    };

    switch (scope) {
    case Scope::Sentence:
        for (int i = from; i <= last; ++i)
            out << makeSpan(i, i);
        break;
    case Scope::Paragraph:
        for (int i = from; i <= last; i += n)
            out << makeSpan(i, qMin(i + n - 1, last));
        break;
    case Scope::Chunk:
        for (int i = from; i <= last; ++i) {
            const Sentence &s = sentences.at(i);
            const QVector<ChunkSlice> slices = chunkSlicesOf(s);
            if (slices.isEmpty()) {
                out << makeSpan(i, i); // 退化：整句作一段
                continue;
            }
            for (int c = 0; c < slices.size(); ++c) {
                Segment seg;
                seg.firstSentence = i;
                seg.lastSentence = i;
                seg.start = slices.at(c).range.start;
                seg.end = slices.at(c).range.end;
                seg.chunkIndex = c;
                seg.chunkCount = slices.size();
                seg.text = slices.at(c).text;
                seg.translation = s.translation;
                out << seg;
            }
        }
        break;
    case Scope::FullScript: {
        Segment seg = makeSpan(from, last);
        seg.wholeScript = true;
        out << seg;
        break;
    }
    }

    if (maxSegments > 0 && out.size() > maxSegments)
        out = out.mid(0, maxSegments);
    return out;
}

// ---------- 会话控制 ----------

void RetellSession::begin(int fromIndex)
{
    beginWith(fromIndex, m_config.scope, m_config.paragraphSize, m_config.recordAudio);
}

void RetellSession::beginWith(int fromIndex, Scope scope, int paragraphSize, bool recordAudio)
{
    if (m_state != State::Idle)
        stop(); // 静默收尾上一轮（会恢复文本、取消录音）

    m_config.scope = scope;
    m_config.paragraphSize = qMax(1, paragraphSize);
    m_config.recordAudio = recordAudio;
    m_segments.clear();
    m_index = -1;

    if (!m_sentences || m_sentences->isEmpty()) {
        emit notice(QStringLiteral("没有可复述的内容：请先打开媒体并加载字幕/生成断句"));
        setState(State::Idle);
        return;
    }
    m_segments = buildSegments(*m_sentences, m_config.scope, fromIndex, m_config.paragraphSize,
                               m_config.maxSegments);
    if (m_segments.isEmpty()) {
        emit notice(QStringLiteral("没有可复述的内容（所选范围为空）"));
        setState(State::Idle);
        return;
    }

    m_index = 0;
    emit notice(QStringLiteral("段落复述开始：%1，共 %2 段%3")
                    .arg(scopeDisplayName(m_config.scope))
                    .arg(m_segments.size())
                    .arg(canRecord() ? QStringLiteral("（可录音复述）")
                                     : QStringLiteral("（静默回忆模式）")));
    startSegment();
}

void RetellSession::stop()
{
    if (m_state == State::Idle) {
        setConcealed(false);
        return;
    }
    if (m_state == State::Retelling)
        emit commandCancelBufferRecording();
    else if (m_state == State::PlayingModel)
        emit commandPause();
    setConcealed(false); // 中途退出必须恢复原文（否则字幕一直隐藏）
    m_index = -1;
    m_segments.clear();
    setState(State::Idle);
}

void RetellSession::nextSegment()
{
    if (m_state == State::Idle || m_state == State::Finished)
        return;
    if (m_state == State::PlayingModel)
        emit commandPause();
    if (m_state == State::Retelling)
        emit commandStopBufferRecording();
    setConcealed(false);
    advance(); // 跳过不写历史（只有自评过的段才进历史）
}

// ---------- 状态推进 ----------

void RetellSession::startSegment()
{
    if (m_index < 0 || m_index >= m_segments.size()) {
        advance();
        return;
    }
    const Segment &seg = m_segments.at(m_index);
    m_quality = -1;
    m_recorded = false;
    m_score = ShadowingScore{};
    m_pendingRecordingPath.clear();

    emit segmentChanged(m_index, seg);
    setConcealed(true); // 播放阶段即隐藏原文，避免「抄答案」
    setState(State::PlayingModel);
    emit commandPlayRange(seg.start, seg.end);
}

void RetellSession::tick(Ms pos)
{
    if (m_state != State::PlayingModel)
        return;
    const Segment *seg = currentSegment();
    if (seg && pos >= seg->end)
        onModelFinished();
}

void RetellSession::onModelFinished()
{
    if (m_state != State::PlayingModel)
        return;
    emit commandPause();
    setState(State::Concealed);
    if (!canRecord()) {
        emit notice(m_config.micAvailable
                        ? QStringLiteral("原文已隐藏：静默回忆后点「揭晓对照」（已关闭录音）")
                        : QStringLiteral("未检测到麦克风：已降级为「静默回忆 → 揭晓对照 → 自评」"));
    } else {
        emit notice(QStringLiteral("原文已隐藏：先默想/默读，再点「开始复述」录音"));
    }
}

void RetellSession::onUserStartRetelling()
{
    if (m_state != State::Concealed)
        return;
    if (!canRecord()) {
        // 降级路径：无麦克风或用户选择不录音 → 跳过录音，直接揭晓对照
        emit notice(m_config.micAvailable
                        ? QStringLiteral("已关闭录音：跳过复述录音，直接揭晓对照")
                        : QStringLiteral("无可用麦克风：跳过录音，直接揭晓对照"));
        reveal();
        return;
    }
    m_recorded = false;
    m_score = ShadowingScore{};
    setState(State::Retelling);
    emit commandStartBufferRecording();
}

void RetellSession::onUserStopRetelling()
{
    if (m_state != State::Retelling)
        return;
    emit commandStopBufferRecording(); // 数据由 AppController 经 onRecordingData 回灌
}

void RetellSession::reveal()
{
    if (m_state == State::Retelling) {
        emit commandStopBufferRecording();
    } else if (m_state != State::Concealed) {
        return;
    }
    setState(State::Revealed);
    setConcealed(false);
    if (m_recorded && m_score.score > 0) {
        emit notice(QStringLiteral("揭晓对照：包络比对 %1 分（%2）")
                        .arg(m_score.score)
                        .arg(m_score.detail));
    } else if (m_recorded) {
        emit notice(QStringLiteral("揭晓对照：%1")
                        .arg(m_score.detail.isEmpty() ? QStringLiteral("未能比对") : m_score.detail));
    } else {
        emit notice(QStringLiteral("揭晓对照：请对照原文自查，然后点自评（0–5）"));
    }
}

void RetellSession::onRecordingData(const QVector<float> &userPcm, int userSr,
                                    const QVector<float> &modelPcm, int modelSr)
{
    if (m_state != State::Retelling && m_state != State::Revealed)
        return;
    m_recorded = !userPcm.isEmpty();
    if (m_recorded) {
        // 与跟读同一套启发式包络比对（docs/08 第 2 节口径，不另造评分）
        m_score = AudioAnalysis::compare(modelPcm, modelSr, userPcm, userSr);
        if (m_score.score <= 0)
            m_score.detail = QStringLiteral("未检测到有效复述语音");
    } else {
        m_score = ShadowingScore{};
        m_score.detail = QStringLiteral("未检测到有效复述语音（未录到声音）");
    }
    if (m_state == State::Retelling) {
        setState(State::Revealed);
        setConcealed(false);
    }
    emit notice(QStringLiteral("复述比对：%1")
                    .arg(m_score.detail.isEmpty()
                             ? QStringLiteral("%1 分").arg(m_score.score)
                             : QStringLiteral("%1 分 · %2").arg(m_score.score).arg(m_score.detail)));
}

void RetellSession::rate(int quality)
{
    if (m_state != State::Revealed && m_state != State::SelfRating)
        return; // 未揭晓不得评分（避免凭空写历史）
    m_quality = qBound(0, quality, 5);
    setState(State::SelfRating);
    const Item it = buildItem();
    appendHistory(it);
    emit resultReady(it);
    advance();
}

// ---------- 内部 ----------

const RetellSession::Segment *RetellSession::currentSegment() const
{
    if (m_index < 0 || m_index >= m_segments.size())
        return nullptr;
    return &m_segments.at(m_index);
}

void RetellSession::advance()
{
    if (m_segments.isEmpty() || m_index + 1 >= m_segments.size()) {
        setConcealed(false);
        setState(State::Finished);
        emit finished();
        return;
    }
    ++m_index;
    startSegment();
}

RetellSession::Item RetellSession::buildItem() const
{
    Item it;
    const Segment *seg = currentSegment();
    it.media = m_mediaTitle;
    it.scope = scopeCode(m_config.scope);
    it.firstSentence = seg ? seg->firstSentence : -1;
    it.lastSentence = seg ? seg->lastSentence : -1;
    it.quality = m_quality;
    it.score = m_recorded ? m_score.score : -1;
    it.detail = m_score.detail;
    it.recorded = m_recorded;
    it.recordingPath = m_pendingRecordingPath;
    it.at = QDateTime::currentMSecsSinceEpoch();
    it.text = seg ? seg->text : QString();
    return it;
}

void RetellSession::appendHistory(const Item &it)
{
    m_history.append(it);
    while (m_history.size() > kMaxHistory)
        m_history.removeFirst();
    emit historyChanged();
}

void RetellSession::clearHistory()
{
    if (m_history.isEmpty())
        return;
    m_history.clear();
    emit historyChanged();
}

void RetellSession::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged(m_state);
}

void RetellSession::setConcealed(bool on)
{
    if (m_concealed == on)
        return;
    m_concealed = on;
    emit concealChanged(m_concealed);
}

// ---------- 历史持久化 ----------

bool RetellSession::loadHistoryFrom(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    f.close();
    if (perr.error != QJsonParseError::NoError || !doc.isArray())
        return false;

    m_history.clear();
    for (const QJsonValue &v : doc.array()) {
        const QJsonObject o = v.toObject();
        Item it;
        it.media = o.value(QStringLiteral("media")).toString();
        it.scope = o.value(QStringLiteral("scope")).toString();
        it.firstSentence = o.value(QStringLiteral("firstSentence")).toInt(-1);
        it.lastSentence = o.value(QStringLiteral("lastSentence")).toInt(-1);
        it.quality = o.value(QStringLiteral("quality")).toInt(-1);
        it.score = o.value(QStringLiteral("score")).toInt(-1);
        it.detail = o.value(QStringLiteral("detail")).toString();
        it.recorded = o.value(QStringLiteral("recorded")).toBool();
        it.recordingPath = o.value(QStringLiteral("recordingPath")).toString();
        it.at = qint64(o.value(QStringLiteral("at")).toDouble());
        it.text = o.value(QStringLiteral("text")).toString();

        // 脏数据丢弃：句区间非法
        if (it.firstSentence < 0 || it.lastSentence < it.firstSentence)
            continue;
        if (it.scope.isEmpty())
            it.scope = scopeCode(Scope::Sentence);
        it.quality = qBound(-1, it.quality, 5);
        if (it.score > 100)
            it.score = -1;
        m_history.append(it);
    }
    while (m_history.size() > kMaxHistory)
        m_history.removeFirst();
    emit historyChanged();
    return true;
}

bool RetellSession::saveHistoryTo(const QString &path) const
{
    QJsonArray arr;
    for (const Item &it : m_history) {
        QJsonObject o;
        o.insert(QStringLiteral("media"), it.media);
        o.insert(QStringLiteral("scope"), it.scope);
        o.insert(QStringLiteral("firstSentence"), it.firstSentence);
        o.insert(QStringLiteral("lastSentence"), it.lastSentence);
        o.insert(QStringLiteral("quality"), it.quality);
        o.insert(QStringLiteral("score"), it.score);
        o.insert(QStringLiteral("detail"), it.detail);
        o.insert(QStringLiteral("recorded"), it.recorded);
        o.insert(QStringLiteral("recordingPath"), it.recordingPath);
        o.insert(QStringLiteral("at"), double(it.at));
        o.insert(QStringLiteral("text"), it.text);
        arr.append(o);
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    f.close();
    return true;
}

} // namespace adoloop
