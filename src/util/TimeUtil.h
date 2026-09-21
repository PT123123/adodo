#pragma once

#include "../core/Types.h"
#include <QString>

namespace adoloop::timeutil {

// 毫秒 → "HH:MM:SS,mmm"（SRT）或 "mm:ss.cc"（LRC）
QString toSrt(Ms ms);
QString toVtt(Ms ms);
QString toLrc(Ms ms);
QString toClock(Ms ms); // 通用 "HH:MM:SS"

// 解析 "HH:MM:SS,mmm" / "HH:MM:SS.mmm" / "mm:ss.xx" / "mm:ss"
Ms fromTimestamp(const QString &s, bool *ok = nullptr);

} // namespace adoloop::timeutil
