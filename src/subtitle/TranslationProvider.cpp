#include "TranslationProvider.h"
#include "../net/HttpClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>

namespace adoloop {

TranslationProvider::TranslationProvider(HttpClient *http, QObject *parent)
    : QObject(parent)
    , m_http(http)
{
}

void TranslationProvider::translate(const QString &text, const QString &from,
                                    const QString &to, quint64 token)
{
    if (!m_http) {
        emit failed(token, QStringLiteral("HTTP 客户端不可用"));
        return;
    }
    if (text.trimmed().isEmpty()) {
        emit finished(token, QString());
        return;
    }

    QUrl url(QStringLiteral("https://fanyi.youdao.com/translate"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("doctype"), QStringLiteral("json"));
    q.addQueryItem(QStringLiteral("type"), QStringLiteral("AUTO"));
    q.addQueryItem(QStringLiteral("i"), text);
    q.addQueryItem(QStringLiteral("from"), from);
    q.addQueryItem(QStringLiteral("to"), to);
    url.setQuery(q);

    m_http->get(url, this, [this, token](const QByteArray &body, int status,
                                         const QString &err) {
        if (status != 200 || !err.isEmpty()) {
            emit failed(token, err.isEmpty() ? QStringLiteral("HTTP %1").arg(status) : err);
            return;
        }
        onReply(token, body);
    });
}

void TranslationProvider::onReply(quint64 token, const QByteArray &body)
{
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        emit failed(token, QStringLiteral("翻译响应解析失败"));
        return;
    }
    const QJsonObject root = doc.object();
    const QJsonArray arr = root.value(QStringLiteral("translateResult")).toArray();
    QStringList parts;
    for (const QJsonValue &outer : arr) {
        const QJsonArray inner = outer.toArray();
        for (const QJsonValue &v : inner) {
            const QString t = v.toObject().value(QStringLiteral("tgt")).toString();
            if (!t.isEmpty())
                parts << t;
        }
    }
    if (parts.isEmpty()) {
        emit failed(token, QStringLiteral("翻译结果为空"));
        return;
    }
    emit finished(token, parts.join(' '));
}

} // namespace adoloop
