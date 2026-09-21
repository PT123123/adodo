#pragma once

#include "../core/Sentence.h"
#include "../core/Types.h"

#include <QString>
#include <QVector>

namespace adoloop {

// 字幕解析：SRT / VTT / LRC / ASS(基础) → QVector<Sentence>
namespace SubtitleParser {

enum class Format { Srt, Vtt, Lrc, Ass, Unknown };

Format detectByPath(const QString &path);
Format detectByContent(const QString &text);

// 解析文件；失败返回空并填充 error
QVector<Sentence> parseFile(const QString &path, QString *error = nullptr);

// 从文本解析（测试用）
QVector<Sentence> parseText(const QString &text, Format fmt, QString *error = nullptr);

} // namespace SubtitleParser
} // namespace adoloop
