#pragma once

#include "../core/Types.h"

#include <QObject>
#include <QString>

namespace adoloop {

class HttpClient;

// 在线词典 Provider（即点即查 / 听音查字入口）。
// v1 实现：有道 jsonapi（免 key，非官方接口，仅供个人学习）。
// M11：按学习语言取不同节点——日语 le=jap 走 jc（表记/假名/声调/发音），
//      英语 le=en 仍走 ec（音标/释义/例句），解析行为与 M10 前一致。
class DictionaryProvider : public QObject {
    Q_OBJECT
public:
    explicit DictionaryProvider(HttpClient *http, QObject *parent = nullptr);

    // 学习语言（app 层注入；默认日语）
    void setLanguage(Lang lang) { m_lang = lang; }
    Lang language() const { return m_lang; }

    // 异步查词；结果经 signals 回传（quint64 token 关联请求）
    void lookup(const QString &word, quint64 token);

signals:
    void finished(quint64 token, const adoloop::DictEntry &entry);
    void failed(quint64 token, const QString &error);

private:
    void onReply(quint64 token, const QByteArray &body);

    HttpClient *m_http = nullptr;
    Lang m_lang = Lang::Japanese;
};

} // namespace adoloop
