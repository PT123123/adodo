#pragma once

#include "../core/Types.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace adoloop {

// 造句练习（Aboboo 练造句）：给定句子，打乱词序，用户重组。
// 纯逻辑，UI（SentenceBuilderWidget）直接消费。
class SentenceBuilder {
public:
    // 学习语言（日语：按分词器切 chip、重组时不插空格；英语：与 M10 前一致）
    void setLang(Lang lang) { m_lang = lang; }
    Lang lang() const { return m_lang; }

    void load(const QString &sentence);
    void reset();

    const QStringList &sourceChips() const { return m_source; } // 待选区
    const QStringList &answerChips() const { return m_answer; } // 已选区
    bool isSolved() const { return m_source.isEmpty(); }

    void pick(int sourceIndex);      // 待选 → 答案
    void unpick(int answerIndex);    // 答案 → 待选（放回末尾）
    QString currentAnswer() const { return m_answer.join(separator()); }
    bool check() const;              // 与原文比较
    QString expected() const { return m_expected; }
    int sourceCount() const { return m_source.size(); }
    int answerCount() const { return m_answer.size(); }

private:
    QString separator() const { return m_lang == Lang::Japanese ? QString() : QStringLiteral(" "); }

    QString m_expected;
    QStringList m_source;
    QStringList m_answer;
    Lang m_lang = Lang::Japanese;
};

} // namespace adoloop
