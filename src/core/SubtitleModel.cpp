#include "SubtitleModel.h"
#include "../subtitle/SubtitleParser.h"
#include "../subtitle/SubtitleAligner.h"
#include "../util/TimeUtil.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringConverter>
#include <QTextStream>

namespace adoloop {

SubtitleModel::SubtitleModel(QObject *parent)
    : QObject(parent)
{
}

void SubtitleModel::setSentences(const QVector<Sentence> &s)
{
    m_sentences = s;
    reindexIds();
    applyLang();
    emit changed();
}

void SubtitleModel::setStudyLang(Lang lang)
{
    if (m_lang == lang)
        return;
    m_lang = lang;
    applyLang();
    emit changed();
}

void SubtitleModel::applyLang()
{
    for (Sentence &s : m_sentences) {
        if (s.lang != m_lang) {
            s.lang = m_lang;
            s.tokens.clear(); // 语言变了，旧分词结果作废（下次按新语言现场分词）
        }
    }
}

int SubtitleModel::sentenceIndexAt(Ms t) const
{
    // 二分：最后一个 start <= t 且 end > t 的句子
    int lo = 0, hi = int(m_sentences.size()) - 1, ans = -1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (m_sentences[mid].start <= t) {
            ans = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    if (ans >= 0 && m_sentences[ans].end <= t)
        return -1; // 落在句间空隙
    return ans;
}

Ms SubtitleModel::endTime() const
{
    Ms end = 0;
    for (const Sentence &s : m_sentences)
        end = qMax(end, s.end);
    return end;
}

bool SubtitleModel::loadFile(const QString &path, QString *error)
{
    const QVector<Sentence> parsed = SubtitleParser::parseFile(path, error);
    if (parsed.isEmpty() && (error == nullptr || error->isEmpty()))
        return false;
    if (parsed.isEmpty())
        return false;
    setSentences(parsed);
    return true;
}

bool SubtitleModel::saveSrt(const QString &path, bool includeTranslation, QString *error) const
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    int idx = 1;
    for (const Sentence &s : m_sentences) {
        out << idx << '\n';
        out << timeutil::toSrt(s.start) << " --> " << timeutil::toSrt(s.end) << '\n';
        out << s.text.trimmed() << '\n';
        if (includeTranslation && !s.translation.isEmpty())
            out << s.translation.trimmed() << '\n';
        out << '\n';
        ++idx;
    }
    f.close();
    return true;
}

void SubtitleModel::shift(Ms deltaMs)
{
    if (deltaMs == 0 || m_sentences.isEmpty())
        return;
    for (Sentence &s : m_sentences) {
        s.start = qMax<Ms>(0, s.start + deltaMs);
        s.end = qMax<Ms>(0, s.end + deltaMs);
    }
    emit changed();
}

int SubtitleModel::mergeBilingual(const SubtitleModel &translationTrack)
{
    const auto &tl = translationTrack.sentences();
    if (tl.isEmpty())
        return 0;

    int matched = 0;
    for (Sentence &s : m_sentences) {
        // 收集所有与 [start,end] 重叠的译文行
        QStringList parts;
        for (const Sentence &t : tl) {
            const Ms overlap = qMin(s.end, t.end) - qMax(s.start, t.start);
            if (overlap > 0 && overlap > (t.duration() / 2)) {
                parts << t.text.trimmed();
            }
        }
        if (!parts.isEmpty()) {
            s.translation = parts.join(' ');
            ++matched;
        }
    }
    emit changed();
    return matched;
}

void SubtitleModel::importAsrSegments(const QVector<AsrSegment> &segments, int maxGapMs)
{
    setSentences(SubtitleAligner::sentencesFromAsr(segments, maxGapMs));
}

void SubtitleModel::mergeWithNext(int idx)
{
    if (idx < 0 || idx + 1 >= m_sentences.size())
        return;
    Sentence &a = m_sentences[idx];
    const Sentence &b = m_sentences[idx + 1];
    const QString sep = a.japanese() ? QString() : QStringLiteral(" ");
    a.end = b.end;
    a.text = (a.text.trimmed() + sep + b.text.trimmed()).trimmed();
    a.words += b.words;
    a.tokens.clear(); // 文本变了：分词结果作废
    if (!a.translation.isEmpty() && !b.translation.isEmpty())
        a.translation = (a.translation.trimmed() + ' ' + b.translation.trimmed()).trimmed();
    m_sentences.removeAt(idx + 1);
    reindexIds();
    emit changed();
}

void SubtitleModel::splitAt(int idx, Ms t)
{
    if (idx < 0 || idx >= m_sentences.size())
        return;
    Sentence &s = m_sentences[idx];
    if (t <= s.start || t >= s.end)
        return;

    // 在 t 处按词切分：尽量在词边界
    Ms cut = t;
    const QVector<WordToken> ws = s.estimatedWords();
    for (const WordToken &w : ws) {
        if (qAbs(w.start - t) < 300) {
            cut = w.start;
            break;
        }
    }

    Sentence b = s;
    b.id = -1;
    b.start = cut;

    // 文本切分：按词时间戳
    QStringList leftText, rightText;
    for (const WordToken &w : s.estimatedWords()) {
        if (w.start < cut)
            leftText << w.word;
        else
            rightText << w.word;
    }
    if (leftText.isEmpty() || rightText.isEmpty())
        return; // 切不出有效内容

    s.end = cut;
    const QString sep = s.japanese() ? QString() : QStringLiteral(" ");
    s.text = leftText.join(sep).trimmed();
    b.text = rightText.join(sep).trimmed();
    s.tokens.clear();
    b.tokens.clear();

    QVector<WordToken> lw, rw;
    for (const WordToken &w : s.words) {
        if (w.start < cut)
            lw << w;
    }
    for (const WordToken &w : b.words) {
        if (w.start >= cut)
            rw << w;
    }
    s.words = lw;
    b.words = rw;

    m_sentences.insert(idx + 1, b);
    reindexIds();
    emit changed();
}

QStringList SubtitleModel::probeSidecarSubs(const QString &mediaPath)
{
    QStringList out;
    const QFileInfo fi(mediaPath);
    const QString base = fi.completeBaseName();
    const QDir dir = fi.dir();
    const QStringList exts = {QStringLiteral("srt"), QStringLiteral("vtt"),
                              QStringLiteral("lrc"), QStringLiteral("ass"),
                              QStringLiteral("ssa")};
    for (const QString &ext : exts) {
        const QString p = dir.filePath(base + '.' + ext);
        if (QFileInfo::exists(p))
            out << p;
    }
    return out;
}

void SubtitleModel::reindexIds()
{
    for (int i = 0; i < m_sentences.size(); ++i)
        m_sentences[i].id = i;
}

} // namespace adoloop
