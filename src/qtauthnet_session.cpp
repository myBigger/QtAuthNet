#include <QtAuthNet/qtauthnet_session.h>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>

namespace QtAuthNet {

class CasSession::Private {
public:
    QString casUrl;
    QString tgt;
    QString username;
    bool loggedIn = false;
    QNetworkAccessManager* nam = nullptr;
    QTimer* renewTimer = nullptr;
    int renewIntervalSec = 3600;
    StatusCallback statusCallback;
    ErrorCallback errorCallback;

    QString buildCasUrl(const QString& path) const {
        QString url = casUrl;
        if (!url.endsWith('/')) url += '/';
        url += "v1/tickets";
        if (!path.isEmpty()) {
            if (!path.startsWith('/')) url += '/';
            url += path;
        }
        return QUrl(url).toString();
    }
};

CasSession::CasSession(const QString& casUrl, QObject* parent)
    : QObject(parent), d(new Private)
{
    d->casUrl = casUrl;
    d->nam = new QNetworkAccessManager(this);
    d->renewTimer = new QTimer(this);
    connect(d->renewTimer, &QTimer::timeout, this, &CasSession::onRenewTimeout);
}

CasSession::~CasSession() { d->renewTimer->stop(); }

void CasSession::setStatusCallback(StatusCallback cb) { d->statusCallback = std::move(cb); }
void CasSession::setErrorCallback(ErrorCallback cb) { d->errorCallback = std::move(cb); }

void CasSession::onRenewTimeout() { renew(nullptr); }

void CasSession::login(const QString& username, const QString& password,
                       const std::function<void(bool)>& callback) {
    d->username = username;
    QUrl url(d->buildCasUrl("/" + username));
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));

    QUrlQuery body;
    body.addQueryItem(QStringLiteral("username"), username);
    body.addQueryItem(QStringLiteral("password"), password);

    QNetworkReply* reply = d->nam->post(request, body.toString(QUrl::FullyEncoded).toUtf8());

    connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            if (d->errorCallback) d->errorCallback(
                QStringLiteral("CAS 登录失败: %1").arg(reply->errorString()));
            if (callback) callback(false);
            return;
        }

        QUrl redirectUrl = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        QString tgtUrl;
        if (redirectUrl.isValid()) {
            tgtUrl = redirectUrl.toString();
        } else {
            QString resp = QString::fromUtf8(reply->readAll()).trimmed();
            if (resp.contains("TGT-")) tgtUrl = d->casUrl + "/v1/tickets/" + resp;
        }

        if (tgtUrl.contains("TGT-")) {
            d->tgt = tgtUrl;
            d->loggedIn = true;
            d->renewTimer->start(d->renewIntervalSec * 1000);
            if (d->statusCallback) d->statusCallback(true);
            if (callback) callback(true);
        } else {
            if (d->errorCallback) d->errorCallback(QStringLiteral("CAS 登录失败: 无法获取 TGT"));
            if (callback) callback(false);
        }
    });
}

bool CasSession::isLoggedIn() const { return d->loggedIn && !d->tgt.isEmpty(); }

void CasSession::logout() {
    if (d->tgt.isEmpty()) return;
    QNetworkRequest request{QUrl(d->tgt)};
    QNetworkReply* reply = d->nam->deleteResource(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        d->tgt.clear(); d->loggedIn = false; d->renewTimer->stop();
        if (d->statusCallback) d->statusCallback(false);
    });
}

void CasSession::renew(const std::function<void(bool)>& callback) {
    if (d->tgt.isEmpty()) { if (callback) callback(false); return; }
    QNetworkRequest request{QUrl(d->tgt)};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
    QNetworkReply* reply = d->nam->post(request, QByteArray());

    connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        reply->deleteLater();
        bool ok = (reply->error() == QNetworkReply::NoError);
        if (ok) {
            d->renewTimer->start(d->renewIntervalSec * 1000);
        } else {
            d->loggedIn = false; d->tgt.clear(); d->renewTimer->stop();
            if (d->errorCallback) d->errorCallback(QStringLiteral("CAS 会话续期失败，需重新登录"));
            if (d->statusCallback) d->statusCallback(false);
        }
        if (callback) callback(ok);
    });
}

void CasSession::get(const QString& path,
                     const std::function<void(const QByteArray&)>& callback) {
    if (!isLoggedIn()) {
        if (d->errorCallback) d->errorCallback(QStringLiteral("未登录"));
        if (callback) callback(QByteArray());
        return;
    }
    acquireServiceTicket(d->buildCasUrl(path), [this, path, callback](const QString& st) {
        if (st.isEmpty()) { if (callback) callback(QByteArray()); return; }
        doGetWithST(path, st, callback);
    });
}

void CasSession::post(const QString& path, const QByteArray& body,
                      const std::function<void(const QByteArray&)>& callback) {
    if (!isLoggedIn()) {
        if (d->errorCallback) d->errorCallback(QStringLiteral("未登录"));
        if (callback) callback(QByteArray());
        return;
    }
    acquireServiceTicket(d->buildCasUrl(path), [this, path, body, callback](const QString& st) {
        if (st.isEmpty()) { if (callback) callback(QByteArray()); return; }
        doPostWithST(path, st, body, callback);
    });
}

void CasSession::acquireServiceTicket(const QString& service,
                                       const std::function<void(const QString&)>& callback) {
    QNetworkRequest request{QUrl(d->tgt)};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
    QUrlQuery body; body.addQueryItem(QStringLiteral("service"), service);
    QNetworkReply* reply = d->nam->post(request, body.toString(QUrl::FullyEncoded).toUtf8());

    connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError)
            callback(QString::fromUtf8(reply->readAll()).trimmed());
        else {
            if (d->errorCallback) d->errorCallback(
                QStringLiteral("获取 Service Ticket 失败: %1").arg(reply->errorString()));
            callback(QString());
        }
    });
}

void CasSession::doGetWithST(const QString& path, const QString& st,
                              const std::function<void(const QByteArray&)>& callback) {
    QUrl url{d->buildCasUrl(path)};
    QUrlQuery q{url}; q.addQueryItem(QStringLiteral("ticket"), st); url.setQuery(q);
    QNetworkRequest request{url};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("QtAuthNet-CAS/1.0"));
    QNetworkReply* reply = d->nam->get(request);

    connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
        reply->deleteLater();
        callback(reply->error() == QNetworkReply::NoError ? reply->readAll() : QByteArray());
    });
}

void CasSession::doPostWithST(const QString& path, const QString& st,
                               const QByteArray& body,
                               const std::function<void(const QByteArray&)>& callback) {
    QUrl url{d->buildCasUrl(path)};
    QUrlQuery q{url}; q.addQueryItem(QStringLiteral("ticket"), st); url.setQuery(q);
    QNetworkRequest request{url};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
    QNetworkReply* reply = d->nam->post(request, body);

    connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
        reply->deleteLater();
        callback(reply->error() == QNetworkReply::NoError ? reply->readAll() : QByteArray());
    });
}

} // namespace QtAuthNet
