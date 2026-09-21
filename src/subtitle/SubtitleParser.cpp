#include "SubtitleParser.h"
#include "../util/TimeUtil.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

namespace adoloop::SubtitleParser {

namespace {
const QString kSrtTime = QStringLiteral(
    "^(\\d{1,2}:\\d{2}:\\d{2}[,.]\\d{1,3})\\s*-->\\s*(\\d{1,2}:\\d{2}:\\d{2}[,.]\\d{1,3})");
const QRegularExpression kTimeArrow(kSrtTime);
const QRegularExpression kAssDialogue(
    QStringLiteral("^Dialogue:\\s*\\d+,\\s*(\\d+:\\d{2}:\\d{2}[.]\\d{2})\\s*,\\s*(\\d+:\\d{2}:\\d{2}[.]\\d{2})"));
const QRegularExpression kLrcLine(QStringLiteral("^\\[(\\d{1,3}:\\d{2}(?:[.]\\d{1,3})?)\\](.*)$"));

QString cleanAss(QString s)
{
    static const QRegularExpression tag(QStringLiteral("\\{[^}]*\\}"));
    return s.remove(tag).replace(QLatin1String("\\N"), QLatin1String(" ")).trimmed();
}
} // namespace

Format detectByPath(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == QLatin1String("srt"))
        return Format::Srt;
    if (ext == QLatin1String("vtt"))
        return Format::Vtt;
    if (ext == QLatin1String("lrc"))
        return Format::Lrc;
    if (ext == QLatin1String("ass") || ext == QLatin1String("ssa"))
        return Format::Ass;
    return Format::Unknown;
}

Format detectByContent(const QString &text)
{
    if (text.startsWith(QLatin1String("WEBVTT")))
        return Format::Vtt;
    const QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    for (const QString &l : lines) {
        if (kAssDialogue.match(l.trimmed()).hasMatch())
            return Format::Ass;
        if (kTimeArrow.match(l.trimmed()).hasMatch())
            return Format::Srt;
        if (kLrcLine.match(l.trimmed()).hasMatch())
            return Format::Lrc;
    }
    return Format::Unknown;
}

QVector<Sentence> parseFile(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = f.errorString();
        return {};
    }
    const QString text = QString::fromUtf8(f.readAll());
    f.close();
    return parseText(text, detectByPath(path), error);
}

QVector<Sentence> parseText(const QString &text, Format fmt, QString *error)
{
    QVector<Sentence> out;
    const QStringList lines = text.split('\n');

    if (fmt == Format::Srt || fmt == Format::Vtt) {
        int i = 0;
        const int n = lines.size();
        while (i < n) {
            const QString line = lines[i].trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1String("WEBVTT"))
                || (line.startsWith(QLatin1String("NOTE")))) {
                ++i;
                continue;
            }
            const auto m = kTimeArrow.match(line);
            if (!m.hasMatch()) {
                ++i;
                continue;
            }
            bool ok1 = false, ok2 = false;
            const Ms start = timeutil::fromTimestamp(m.captured(1), &ok1);
            const Ms end = timeutil::fromTimestamp(m.captured(2), &ok2);
            if (!ok1 || !ok2) {
                ++i;
                continue;
            }
            // 收集文本行（直到空行或下一条时间行）
            QStringList textLines;
            int j = i + 1;
            while (j < n && !lines[j].trimmed().isEmpty()) {
                const QString t = lines[j].trimmed();
                if (kTimeArrow.match(t).hasMatch() || t.startsWith(QLatin1String("WEBVTT")))
                    break;
                textLines << t;
                ++j;
            }
            Sentence s;
            s.start = start;
            s.end = qMax(end, start + 1);
            s.text = textLines.join(' ').trimmed();
            if (!s.text.isEmpty())
                out << s;
            i = j;
        }
    } else if (fmt == Format::Lrc) {
        for (const QString &raw : lines) {
            const auto m = kLrcLine.match(raw.trimmed());
            if (!m.hasMatch())
                continue;
            bool ok = false;
            const Ms start = timeutil::fromTimestamp(m.captured(1), &ok);
            if (!ok)
                continue;
            Sentence s;
            s.start = start;
            s.end = start + 3000; // LRC 无结束时间：默认 3s，可经对齐修正
            s.text = m.captured(2).trimmed();
            if (!s.text.isEmpty())
                out << s;
        }
    } else if (fmt == Format::Ass) {
        for (const QString &raw : lines) {
            const auto m = kAssDialogue.match(raw);
            if (!m.hasMatch())
                continue;
            bool ok1 = false, ok2 = false;
            const Ms start = timeutil::fromTimestamp(m.captured(1), &ok1);
            const Ms end = timeutil::fromTimestamp(m.captured(2), &ok2);
            if (!ok1 || !ok2)
                continue;
            // 找最后一个逗号之后的正文（对话字段），去掉样式标签
            const int comma = raw.lastIndexOf(',');
            if (comma < 0)
                continue;
            const QString body = cleanAss(raw.mid(comma + 1));
            if (body.isEmpty())
                continue;
            Sentence s;
            s.start = start;
            s.end = qMax(end, start + 1);
            s.text = body;
            out << s;
        }
    } else {
        if (error)
            *error = QStringLiteral("无法识别的字幕格式");
        return {};
    }

    for (int i = 0; i < out.size(); ++i)
        out[i].id = i;
    return out;
}

} // namespace adoloop::SubtitleParser
