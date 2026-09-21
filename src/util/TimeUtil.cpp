#include "TimeUtil.h"

#include <QRegularExpression>
#include <QStringList>

namespace adoloop::timeutil {

namespace {
QString two(int v) { return QString::number(v).rightJustified(2, '0'); }
QString three(int v) { return QString::number(v).rightJustified(3, '0'); }
} // namespace

QString toSrt(Ms ms)
{
    const qint64 total = qMax<qint64>(0, ms);
    const qint64 h = total / 3600000;
    const qint64 m = (total % 3600000) / 60000;
    const qint64 s = (total % 60000) / 1000;
    const qint64 milli = total % 1000;
    return QStringLiteral("%1:%2:%3,%4")
        .arg(two(int(h)), two(int(m)), two(int(s)), three(int(milli)));
}

QString toVtt(Ms ms)
{
    const qint64 total = qMax<qint64>(0, ms);
    const qint64 h = total / 3600000;
    const qint64 m = (total % 3600000) / 60000;
    const qint64 s = (total % 60000) / 1000;
    const qint64 milli = total % 1000;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(two(int(h)), two(int(m)), two(int(s)), three(int(milli)));
}

QString toLrc(Ms ms)
{
    const qint64 total = qMax<qint64>(0, ms);
    const qint64 m = (total % 3600000) / 60000;
    const qint64 s = (total % 60000) / 1000;
    const qint64 centi = (total % 1000) / 10;
    return QStringLiteral("%1:%2.%3").arg(two(int(m)), two(int(s)), two(int(centi)));
}

QString toClock(Ms ms)
{
    const qint64 total = qMax<qint64>(0, ms);
    const qint64 h = total / 3600000;
    const qint64 m = (total % 3600000) / 60000;
    const qint64 s = (total % 60000) / 1000;
    return QStringLiteral("%1:%2:%3").arg(two(int(h)), two(int(m)), two(int(s)));
}

Ms fromTimestamp(const QString &s, bool *ok)
{
    static const QRegularExpression re(
        QStringLiteral("^(?:(\\d+):)?(\\d{1,2}):(\\d{1,2})(?:[.,](\\d{1,3}))?$"));
    const auto m = re.match(s.trimmed());
    if (!m.hasMatch()) {
        if (ok)
            *ok = false;
        return -1;
    }
    const qint64 h = m.captured(1).isEmpty() ? 0 : m.captured(1).toLongLong();
    const qint64 min = m.captured(2).toLongLong();
    const qint64 sec = m.captured(3).toLongLong();
    QString frac = m.captured(4);
    while (frac.size() < 3)
        frac += '0';
    const qint64 ms = frac.isEmpty() ? 0 : frac.left(3).toLongLong();
    if (ok)
        *ok = true;
    return h * 3600000 + min * 60000 + sec * 1000 + ms;
}

} // namespace adoloop::timeutil
