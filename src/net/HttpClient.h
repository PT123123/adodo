#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>

#include <functional>

namespace adoloop {

// HTTP GET 封装：QNetworkAccessManager + 超时 + 回调（context 对象存活校验）。
// 回调签名：void(QByteArray body, int status, QString error)
class HttpClient : public QObject {
    Q_OBJECT
public:
    explicit HttpClient(QObject *parent = nullptr);

    using ReplyCallback = std::function<void(const QByteArray &, int, const QString &)>;

    void get(const QUrl &url, QObject *context, ReplyCallback cb, int timeoutMs = 15000);
    void get(const QUrl &url, ReplyCallback cb, int timeoutMs = 15000);

private:
    void doGet(const QUrl &url, QObject *context, ReplyCallback cb, int timeoutMs);

    QNetworkAccessManager m_nam;
};

} // namespace adoloop
