#pragma once

#include <QString>

namespace adoloop::japaneseinput {

// 日语输入工具（M11）：罗马字 → 假名（纯函数，可离线单测）。
// 覆盖常见规则：促音 っ（双写辅音 / tch）、拨音 ん（n 后接非元音、nn、n'）、
// 長音 ー（-）、拗音 sha/shu/sho、ja/ju/jo、cha/chu/cho、tsu、小写假名 xa/xtu/lya …。
QString romajiToHiragana(const QString &romaji);
QString romajiToKatakana(const QString &romaji);

// 假名互转（片假名 ⇄ 平假名，含 ー 保留）
QString katakanaToHiragana(const QString &s);
QString hiraganaToKatakana(const QString &s);

// 是否「看起来是罗马字输入」：仅 ASCII 字母/数字/空白/'/-，且含至少一个字母
bool looksLikeRomaji(const QString &s);
// 是否含平假名或片假名
bool containsKana(const QString &s);

// 听音查字/取词的统一入口：罗马字输入转平假名；已是假名/汉字则原样返回
QString normalizeQuery(const QString &input);

} // namespace adoloop::japaneseinput
