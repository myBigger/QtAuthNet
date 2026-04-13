#include "qtauthnet_session.h"
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkCookieJar>
#include <QtCore/QTimer>
#include <QtCore/QUuid>

namespace QtAuthNet {

class CasSession::Private {
public:
    QString casUrl;                         // CAS 服务器地址，如 https://cas.example.com
    QString service;                        // 当前应用的服务地址
    QString tgt;                            // Ticket Granting Ticket
    QString st;                             // Service Ticket（一次性）
    QString username;
    bool loggedIn = false;
    QNetworkAccessManager* nam = nullptr;
    QTimer* renewTimer = nullptr;           // 自动续期定时器
    int renewIntervalSec = 3600;            // 默认 1 小时续期一次

    QMap<QNetworkReply*, std::function<void(const QByteArray&)>> pendingCallbacks;

    QUrl buildCasUrl(const QString& path, const QVariantMap& params = QVariantMap()) const {
        QString url = casUrl;
        if (!url.endsWith('/')) url += '/';
        url += "v1/tickets"; // CAS 2.0 协议
        if (!path.isEmpty()) {
            if (!path.startsWith('/')) url += '/';
            url += path;
        }
        QUrl qurl(url);
        if (!params.isEmpty()) {
            QUrlQuery query;
            for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
                query.addQueryItem(it.key(), it.value().toString());
            }
            qurl.setQuery(query);
        }
        return qurl;
    }
};

CasSession::CasSession(const QString& casUrl, QObject* parent)
    : QObject(parent), d(new Private)
{
    d->casUrl = casUrl;
    d->nam = new QNetworkAccessManager(this);
    d->tgt.clear();
    d->loggedIn = false;

    // 自动续期定时器
    d->renewTimer = new QTimer(this);
    connect(d->renewTimer, &QTimer::timeout, this, &CasSession::renew);
}

CasSession::~CasSession() {
    d->renewTimer->stop();
}

void CasSession::login(const QString& username, const QString& password,
                      const std::function<void(bool success)>& callback) {
    d->username = username;

    // 第一步：获取 TGT (Ticket Granting Ticket)
    QUrl url = d->buildCasUrl("/" + username);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));

    // CAS 2.0 POST 获取 TGT: username=xxx&password=xxx
    QUrlQuery body;
    body.addQueryItem(QStringLiteral("username"), username);
    body.addQueryItem(QStringLiteral("password"), password);
    QByteArray bodyData = body.toString(QUrl::FullyEncoded).toUtf8();

    QNetworkReply* reply = d->nam->post(request, bodyData);

    connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            emit error(QStringLiteral("CAS 登录请求失败: %1").arg(reply->errorString()));
            callback(false);
            return;
        }

        // CAS 返回 TGT 的 URL，格式: https://cas.example.com/v1/tickets/TGT-xxx
        QUrl redirectUrl = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        QByteArray responseData = reply->readAll();
        QString responseText = QString::fromUtf8(responseData);

        // 从 Location header 或 body 中提取 TGT
        QString tgtUrl;
        if (redirectUrl.isValid()) {
            tgtUrl = redirectUrl.toString();
        } else if (responseText.startsWith(QStringLiteral("TGT-"))) {
            tgtUrl = d->casUrl + "/v1/tickets/" + responseText.trimmed();
        }

        if (tgtUrl.contains("TGT-")) {
            d->tgt = tgtUrl;
            d->loggedIn = true;
            d->renewTimer->start(d->renewIntervalSec * 1000);
            emit loginStatusChanged(true);
            callback(true);
        } else {
            emit error(QStringLiteral("CAS 登录失败: 无法获取 TGT"));
            callback(false);
        }
    });
}

bool CasSession::isLoggedIn() const {
    return d->loggedIn && !d->tgt.isEmpty();
}

void CasSession::logout() {
    if (d->tgt.isEmpty()) return;

    // 向 CAS 服务器发送 DELETE 请求销毁 TGT
    QNetworkRequest request(d->tgt);
    request.setHeader(QNetworkRequest::KnownHeaders::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));

    QNetworkReply* reply = d->nam->deleteResource(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        d->tgt.clear();
        d->loggedIn = false;
        d->renewTimer->stop();
        emit loginStatusChanged(false);
    });
}

void CasSession::renew(const std::function<void(bool)>& callback) {
    if (d->tgt.isEmpty()) {
        if (callback) callback(false);
        return;
    }

    // 向 TGT 发送 POST 刷新寿命
    QNetworkRequest request(d->tgt);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));

    QNetworkReply* reply = d->nam->post(request, QByteArray());

    connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        reply->deleteLater();
        bool ok = (reply->error() == QNetworkReply::NoError);
        if (ok) {
            d->renewTimer->start(d->renewIntervalSec * 1000); // 重置定时器
        } else {
            // 续期失败，可能是 TGT 已过期，重新登录
            d->loggedIn = false;
            d->tgt.clear();
            d->renewTimer->stop();
            emit error(QStringLiteral("CAS 会话续期失败，需重新登录"));
            emit loginStatusChanged(false);
        }
        if (callback) callback(ok);
    });
}

void CasSession::get(const QString& path,
                    const std::function<void(const QByteArray&)>& callback) {
    // 如果已登录，先获取 ST，再发起请求
    if (isLoggedIn()) {
        QString serviceUrl = d->buildCasUrl(path).toString();
        acquireServiceTicket(serviceUrl, [this, callback](const QString& st) {
            if (st.isEmpty()) {
                if (callback) callback(QByteArray());
                return;
            }
            doGetWithST(path, st, callback);
        });
    } else {
        emit error(QStringLiteral("未登录，无法发起请求"));
        if (callback) callback(QByteArray());
    }
}

void CasSession::post(const QString& path, const QByteArray& body,
                     const std::function<void(const QByteArray&)>& callback) {
    if (isLoggedIn()) {
        QString serviceUrl = d->buildCasUrl(path).toString();
        acquireServiceTicket(serviceUrl, [this, path, body, callback](const QString& st) {
            if (st.isEmpty()) {
                if (callback) callback(QByteArray());
                return;
            }
            doPostWithST(path, st, body, callback);
        });
    } else {
        emit error(QStringLiteral("未登录，无法发起请求"));
        if (callback) callback(QByteArray());
    }
}

void CasSession::acquireServiceTicket(const QString& service,
                                     const std::function<void(const QString& st)>& callback) {
    // 从 TGT 获取一次性 Service Ticket
    QNetworkRequest request(d->tgt);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));

    QUrlQuery body;
    body.addQueryItem(QStringLiteral("service"), service);
    QByteArray bodyData = body.toString(QUrl::FullyEncoded).toUtf8();

    QNetworkReply* reply = d->nam->post(request, bodyData);

    connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        QByteArray data = reply->readAll();
        reply->deleteLater();

        if (reply->error() == QNetworkReply::NoError) {
            QString st = QString::fromUtf8(data).trimmed();
            callback(st);
        } else {
            emit error(QStringLiteral("获取 Service Ticket 失败: %1").arg(reply->errorString()));
            callback(QString());
        }
    });
}

void CasSession::doGetWithST(const QString& path, const QString& st,
                             const std::function<void(const QByteArray&)>& callback) {
    QUrl url = d->buildCasUrl(path);
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("ticket"), st);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::KnownHeaders::UserAgentHeader,
                      QStringLiteral("QtAuthNet-CAS/1.0"));

    QNetworkReply* reply = d->nam->get(request);

    auto wrapper = [this, reply, callback]() {
        auto it = d->pendingCallbacks.find(reply);
        if (it != d->pendingCallbacks.end()) {
            std::function<void(const QByteArray&)> cb = it.value();
            d->pendingCallbacks.erase(it);
            cb(reply->readAll());
        }
        reply->deleteLater();
    };

    d->pendingCallbacks[reply] = callback;
    connect(reply, &QNetworkReply::finished, this, wrapper);
}

void CasSession::doPostWithST(const QString& path, const QString& st,
                              const QByteArray& body,
                              const std::function<void(const QByteArray&)>& callback) {
    QUrl url = d->buildCasUrl(path);
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("ticket"), st);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                     QStringLiteral("application/x-www-form-urlencoded"));

    QNetworkReply* reply = d->nam->post(request, body);

    auto wrapper = [this, reply, callback]() {
        auto it = d->pendingCallbacks.find(reply);
        if (it != d->pendingCallbacks.end()) {
            std::function<void(const QByteArray&)> cb = it.value();
            d->pendingCallbacks.erase(it);
            cb(reply->readAll());
        }
        reply->deleteLater();
    };

    d->pendingCallbacks[reply] = callback;
    connect(reply, &QNetworkReply::finished, this, wrapper);
}

} // namespace QtAuthNet
