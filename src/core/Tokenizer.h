#pragma once

#include "Types.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace adoloop {

// 分词结果（富信息）。内置启发式档只填 surface/begin/length；
// MeCab / Python 桥档额外填 reading（读音）与 lemma（原形）。
struct Token {
    QString surface;   // 表记（原文中的写法）
    QString reading;   // 读音（假名/音标；内置档为空）
    QString lemma;     // 原形（内置档为空）
    int begin = -1;    // 在原文中的起始位置（UTF-16 码元，供字幕像素级命中）
    int length = 0;    // 长度（UTF-16 码元）
    bool hasReading() const { return !reading.isEmpty(); }
};

// 语言感知分词器（M11）。
//
// 分层约束：本类属 core 层，**不依赖 app/Settings**——外部引擎路径、
// 桥脚本位置等经 Options 由 app 层注入。
//
// 三级降级（日语）：
//   ① MeCab：Options::mecabPath 非空且文件存在 → 子进程按行分词（词/读音/原形）
//   ② Python 桥：Options::pythonPath 非空且可执行 → scripts/japanese_tokenizer_bridge.py
//      （janome / fugashi，逐行 JSON 协议，风格同 faster_whisper_bridge.py）
//   ③ 内置启发式：**默认档**，纯函数、无外部依赖（本机无 MeCab/janome 时走这条）
// 任何一档不可用/失败都静默降级，程序不因缺失外部组件而失败。
//
// 英语（Lang::English）走 strutil::tokenizeWords 的同一规则，行为与 M10 前一致。
class Tokenizer {
public:
    struct Options {
        QString mecabPath;    // 空 = 不尝试 MeCab（Settings tools/mecab）
        QString pythonPath;   // 空 = 不尝试 Python 桥（Settings tools/python）
        QString bridgeScript; // 空 = 默认 <可执行目录>/scripts/japanese_tokenizer_bridge.py
        int timeoutMs = 15000;
        int maxBatchLines = 500; // 单次外部调用最大行数（防超长字幕阻塞）
    };

    // ---------- 语言工具 ----------
    static Lang langFromCode(const QString &code);   // "ja"/"jp"/"japanese" → Japanese，其余 English
    static QString langCode(Lang lang);              // "ja" / "en"
    static QString langDisplayName(Lang lang);       // "日本語" / "English"

    // ---------- 公开入口 ----------
    // 词表（去标点，保序）。日语走内置启发式；英语与 strutil::tokenizeWords 一致。
    static QStringList tokenize(const QString &text, Lang lang);

    // 富结果（内置启发式档：surface/begin/length；reading/lemma 留空）
    static QVector<Token> tokens(const QString &text, Lang lang);

    // 富结果（三级降级：MeCab → Python 桥 → 内置）。engineUsed 回传实际使用的引擎名。
    static QVector<Token> tokensExternal(const QString &text, Lang lang, const Options &opt,
                                         QString *engineUsed = nullptr);
    // 批量版（一次子进程处理多行；字幕加载后统一升级用）
    static QVector<QVector<Token>> tokensExternalBatch(const QStringList &lines, Lang lang,
                                                       const Options &opt,
                                                       QString *engineUsed = nullptr);

    // 造句练习的分词 chip：标点并入前一个词（英语同 strutil::splitToWordChips）
    static QStringList splitChips(const QString &text, Lang lang);

    // ---------- 纯函数（可离线单测） ----------
    static QVector<Token> heuristicJapaneseTokens(const QString &text);
    static QVector<Token> englishTokens(const QString &text);

    // 字符类别判定（内置启发式用；公开以便单测）
    static bool isKanjiChar(QChar c);
    static bool isHiraganaChar(QChar c);
    static bool isKatakanaChar(QChar c);
    static bool isLatinChar(QChar c);
    // 助词/助动词白名单：命中的平假名 run 不并入前邻汉字 run
    static bool isParticleOrAuxiliary(const QString &hiraganaRun);

    // 缺省桥脚本路径（<可执行目录>/scripts/…，回退源码树 scripts/）
    static QString defaultBridgeScriptPath();

private:
    // 返回空表示该档不可用（调用方继续降级）
    static QVector<QVector<Token>> tryMecab(const QStringList &lines, const Options &opt, bool *ok);
    static QVector<QVector<Token>> tryPythonBridge(const QStringList &lines, const Options &opt,
                                                   bool *ok);
};

} // namespace adoloop
