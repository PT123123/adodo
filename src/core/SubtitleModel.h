#pragma once

#include "Types.h"
#include "Sentence.h"

#include <QObject>
#include <QVector>

namespace adoloop {

// 字幕/句子容器：持有全部学习句，提供定位、双语合并、ASR 导入、持久化。
// 解析工作由 subtitle/SubtitleParser 完成（本类只做编排）。
class SubtitleModel : public QObject {
    Q_OBJECT
public:
    explicit SubtitleModel(QObject *parent = nullptr);

    // 全量替换（ASR 导入、断句结果、加载字幕）
    void setSentences(const QVector<Sentence> &s);

    // 学习语言注入（app 层从 Settings 传入；core 层不反向依赖 app）
    void setStudyLang(Lang lang);
    Lang studyLang() const { return m_lang; }

    const QVector<Sentence> &sentences() const { return m_sentences; }
    // 可变访问（翻译写入等内部用途）
    QVector<Sentence> &mutableSentences() { return m_sentences; }
    int count() const { return m_sentences.size(); }
    bool isEmpty() const { return m_sentences.isEmpty(); }
    const Sentence &at(int i) const { return m_sentences.at(i); }

    // 当前时间所在句索引（-1 表示无）
    int sentenceIndexAt(Ms t) const;

    // 全部句子结束时间（0 表示空）
    Ms endTime() const;

    // 解析并加载字幕文件（自动探测格式）
    bool loadFile(const QString &path, QString *error = nullptr);

    // 导出 SRT
    bool saveSrt(const QString &path, bool includeTranslation = false,
                 QString *error = nullptr) const;

    // 整体平移（毫秒，可负）
    void shift(Ms deltaMs);

    // 双语配对：把另一轨字幕按时间匹配到本轨的 translation 字段。
    // 一行原文可能合并多行译文。返回匹配上的行数。
    int mergeBilingual(const SubtitleModel &translationTrack);

    // 从 ASR 结果导入（按 gap 合并成句，见 SubtitleAligner）
    void importAsrSegments(const QVector<AsrSegment> &segments, int maxGapMs = 600);

    // 手动调整断点：合并相邻句 / 在 t 处切分第 idx 句（返回新句列表由调用方使用）
    void mergeWithNext(int idx);
    void splitAt(int idx, Ms t);

    // 数据目录中自动探测同名字幕（mediaPath 去扩展名 + .srt/.vtt/.lrc/.ass）
    static QStringList probeSidecarSubs(const QString &mediaPath);

signals:
    void changed();

private:
    void reindexIds();
    void applyLang(); // 把 m_lang 铺到全部句子（新增/重建句后调用）

    QVector<Sentence> m_sentences;
    Lang m_lang = Lang::Japanese; // 学习语言（默认日语，与 Settings 默认一致）
};

} // namespace adoloop
