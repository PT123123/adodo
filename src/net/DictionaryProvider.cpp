#include "DictionaryProvider.h"
#include "DictionaryParser.h"
#include "HttpClient.h"

#include <QUrl>
#include <QUrlQuery>

namespace adoloop {

DictionaryProvider::DictionaryProvider(HttpClient *http, QObject *parent)
    : QObject(parent)
    , m_http(http)
{
}

void DictionaryProvider::lookup(const QString &word, quint64 token)
{
    const QString w = word.trimmed();
    if (w.isEmpty() || !m_http) {
        emit failed(token, QStringLiteral("无效查询词"));
        return;
    }

    QUrl url(QStringLiteral("https://dict.youdao.com/jsonapi"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), w);
    // 日语：le=jap（jc 节点）；英语：le=en（ec 节点）
    q.addQueryItem(QStringLiteral("le"),
                   m_lang == Lang::Japanese ? QStringLiteral("jap") : QStringLiteral("en"));
    url.setQuery(q);

    m_http->get(url, this, [this, token](const QByteArray &body, int status, const QString &err) {
        if (status != 200 || !err.isEmpty()) {
            emit failed(token, err.isEmpty() ? QStringLiteral("HTTP %1").arg(status) : err);
            return;
        }
        onReply(token, body);
    });
}

void DictionaryProvider::onReply(quint64 token, const QByteArray &body)
{
    DictEntry e;
    QString err;
    const bool ok = m_lang == Lang::Japanese
        ? dictparse::parseJapaneseJson(body, &e, &err)
        : dictparse::parseEnglishJson(body, &e, &err);
    if (!ok || !e.ok()) {
        emit failed(token, err.isEmpty() ? QStringLiteral("未找到释义") : err);
        return;
    }
    emit finished(token, e);
}

} // namespace adoloop
