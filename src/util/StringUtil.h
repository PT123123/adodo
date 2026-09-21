#pragma once

#include <QString>
#include <QStringList>

namespace adoloop::strutil {

// 去掉首尾空白与常见标点
QString stripPunctuation(const QString &s);

// 拆出词（保留词内连字符/撇号），去掉标点
QStringList tokenizeWords(const QString &s);

// 将一段文本切成单词列表（词序保留），用于造句打乱
QStringList splitToWordChips(const QString &s);

// Levenshtein 距离（听写校对用）
int levenshtein(const QString &a, const QString &b);

// 归一化：忽略标点与大小写（听写校对可配置）
QString normalizeForCheck(const QString &s, bool ignorePunctuation, bool ignoreCase);

// 是否为空白/无意义字符
bool isPunct(QChar c);

} // namespace adoloop::strutil
