#include "HttpClient.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>

namespace adoloop {

HttpClient::HttpClient(QObject *parent)
    : QObject(parent)
{
}

void HttpClient::get(const QUrl &url, QObject *context, ReplyCallback cb, int timeoutMs)
{
    doGet(url, context, std::move(cb), timeoutMs);
}

void HttpClient::get(const QUrl &url, ReplyCallback cb, int timeoutMs)
{
    doGet(url, this, std::move(cb), timeoutMs);
}

void HttpClient::doGet(const QUrl &url, QObject *context, ReplyCallback cb, int timeoutMs)
{
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Mozilla/5.0 (AdoLoop/0.1)"));
    QNetworkReply *reply = m_nam.get(req);

    auto *timer = new QTimer(reply);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, reply, [reply]() {
        if (reply->isRunning())
            reply->abort();
    });

    QObject::connect(reply, &QNetworkReply::finished, reply, [=, cb = std::move(cb)]() {
        timer->stop();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        QString err;
        if (reply->error() != QNetworkReply::NoError)
            err = reply->errorString();
        reply->deleteLater();
        const QPointer<QObject> ctx = context;
        if (ctx.isNull()) // 请求方已销毁，丢弃回调
            return;
        cb(body, status, err);
    });
}

} // namespace adoloop
