#pragma once

#include "../core/Types.h"

#include <QByteArray>
#include <QString>

namespace adoloop {

// 有道 jsonapi 响应解析（纯函数，可离线单测）。
// 日语走 jc 节点（表记/假名/声调/发音），英语走 ec 节点（音标/释义/例句）。
// 失败时 error 为中文提示。
namespace dictparse {

// 日语：https://dict.youdao.com/jsonapi?q=<词>&le=jap
//   jc.word[] = { origin, return-phrase.l.i, phonesup, speech, trs[].pos / trs[].tr[].l.i }
bool parseJapaneseJson(const QByteArray &body, DictEntry *out, QString *error);

// 英语：https://dict.youdao.com/jsonapi?q=<word>&le=en
//   ec.word = { return-phrase, usphone, ukphone, phone }、ec.trs[].tr[].l.i、
//   blng_sents_part.sentence-pair[]（例句）
bool parseEnglishJson(const QByteArray &body, DictEntry *out, QString *error);

// 发音音频 URL 拼接
//   speech 字段（形如 "がくせい&le=jap" 或 "word&type=1"，已 URL 编码）→ 完整 URL
QString audioUrlFromSpeech(const QString &speech);
// 兜底：按词/假名自行拼接 dictvoice URL
QString audioUrlForWord(const QString &word, Lang lang, int type = 1);

} // namespace dictparse

} // namespace adoloop
