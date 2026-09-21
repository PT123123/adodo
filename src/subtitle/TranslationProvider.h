#pragma once

#include "../core/Types.h"

#include <QObject>
#include <QString>

namespace adoloop {

class HttpClient;

// 实时翻译 Provider（双语字幕译文、听写参考译文）。
// v1 实现：有道网页接口（免 key，非官方，仅供个人学习；请求带 token 异步回传）。
class TranslationProvider : public QObject {
    Q_OBJECT
public:
    explicit TranslationProvider(HttpClient *http, QObject *parent = nullptr);

    // 异步翻译；结果经 finished/failed 回传（token 用于关联请求）
    void translate(const QString &text, const QString &from, const QString &to,
                   quint64 token);

signals:
    void finished(quint64 token, const QString &translation);
    void failed(quint64 token, const QString &error);

private:
    void onReply(quint64 token, const QByteArray &body);

    HttpClient *m_http = nullptr;
};

} // namespace adoloop
