#include "qtauthnet_client.h"
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QAuthenticator>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QUrlQuery>
#include <QtCore/QTimer>

namespace QtAuthNet {

class Client::Private {
public:
    QString baseUrl;
    QUrl resolvePath(const QString& path) const {
        QString full = baseUrl;
        if (!full.endsWith('/') && !path.startsWith('/'))
            full += '/';
        else if (full.endsWith('/') && path.startsWith('/'))
            full.chop(1);
        return QUrl(full + path);
    }

    QNetworkAccessManager* nam = nullptr;
    QString bearerToken;
    QString basicUser;
    QString basicPass;
    QString apiKey;
    QString apiKeyLocation = QStringLiteral("header"); // "header" or "query"
    QString apiKeyName = QStringLiteral("X-API-Key");
    QMap<QString, QString> customHeaders;
    RefreshCallback refreshCallback;
    QMap<QNetworkReply*, std::function<void(const QByteArray&)>> pendingCallbacks;
    bool isRefreshing = false;
    QQueue<QPair<QString, QPair<QByteArray, std::function<void(const QByteArray&)>>>> pendingRequests;
};

Client::Client(const QString& baseUrl, QObject* parent)
    : QObject(parent), d(new Private)
{
    d->baseUrl = baseUrl;
    d->nam = new QNetworkAccessManager(this);
    d->bearerToken.clear();
    d->basicUser.clear();
    d->basicPass.clear();
}

Client::~Client() = default;

void Client::setBearerToken(const QString& token) {
    d->bearerToken = token;
}

void Client::setBasicAuth(const QString& username, const QString& password) {
    d->basicUser = username;
    d->basicPass = password;
}

void Client::setApiKey(const QString& key, const QString& location) {
    d->apiKey = key;
    d->apiKeyLocation = location;
}

void Client::setHeader(const QString& key, const QString& value) {
    d->customHeaders[key] = value;
}

void Client::setTokenRefreshCallback(RefreshCallback callback) {
    d->refreshCallback = callback;
}

void Client::cancel() {
    qDeleteAll(d->nam->findChildren<QNetworkReply*>());
    d->pendingCallbacks.clear();
    d->pendingRequests.clear();
}

void Client::get(const QString& path, const std::function<void(const QByteArray&)>& callback) {
    executeRequest(QStringLiteral("GET"), path, QByteArray(), callback);
}

void Client::post(const QString& path, const QByteArray& body,
                  const std::function<void(const QByteArray&)>& callback) {
    executeRequest(QStringLiteral("POST"), path, body, callback);
}

void Client::postJson(const QString& path, const QVariant& json,
                     const std::function<void(const QByteArray&)>& callback) {
    QJsonDocument doc = QJsonDocument::fromVariant(json);
    QByteArray body = doc.toJson(QJsonDocument::Compact);
    QNetworkRequest req;
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    executeRequest(QStringLiteral("POST"), path, body, callback);
}

void Client::put(const QString& path, const QByteArray& body,
                 const std::function<void(const QByteArray&)>& callback) {
    executeRequest(QStringLiteral("PUT"), path, body, callback);
}

void Client::deleteResource(const QString& path,
                            const std::function<void(const QByteArray&)>& callback) {
    executeRequest(QStringLiteral("DELETE"), path, QByteArray(), callback);
}

void Client::executeRequest(const QString& method, const QString& path,
                           const QByteArray& body,
                           const std::function<void(const QByteArray&)>& callback) {
    QUrl url = d->resolvePath(path);

    // API Key 注入
    if (!d->apiKey.isEmpty()) {
        if (d->apiKeyLocation == QStringLiteral("query")) {
            QUrlQuery query(url);
            query.addQueryItem(d->apiKeyName, d->apiKey);
            url.setQuery(query);
        }
        // header 方式在下面统一处理
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::KnownHeaders::UserAgentHeader,
                      QStringLiteral("QtAuthNet/1.0"));

    // API Key Header
    if (!d->apiKey.isEmpty() && d->apiKeyLocation == QStringLiteral("header")) {
        request.setRawHeader(d->apiKeyName.toUtf8(), d->apiKey.toUtf8());
    }

    // Bearer Token
    if (!d->bearerToken.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + d->bearerToken.toUtf8());
    }

    // Custom Headers
    for (auto it = d->customHeaders.constBegin(); it != d->customHeaders.constEnd(); ++it) {
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }

    // Content-Type for body requests
    if (!body.isEmpty() && method != QStringLiteral("GET")) {
        if (!request.hasRawHeader("Content-Type")) {
            request.setHeader(QNetworkRequest::ContentTypeHeader,
                             QStringLiteral("application/x-www-form-urlencoded"));
        }
    }

    QNetworkReply* reply = nullptr;

    if (method == QStringLiteral("GET")) {
        reply = d->nam->get(request);
    } else if (method == QStringLiteral("POST")) {
        reply = d->nam->post(request, body);
    } else if (method == QStringLiteral("PUT")) {
        reply = d->nam->put(request, body);
    } else if (method == QStringLiteral("DELETE")) {
        reply = d->nam->deleteResource(request);
    }

    if (!reply) return;

    d->pendingCallbacks[reply] = callback;

    connect(reply, &QNetworkReply::finished, this, &Client::onReplyFinished);
}

void Client::onReplyFinished() {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    auto callbackIt = d->pendingCallbacks.find(reply);
    if (callbackIt == d->pendingCallbacks.end()) {
        reply->deleteLater();
        return;
    }

    std::function<void(const QByteArray&)> callback = callbackIt.value();
    d->pendingCallbacks.erase(callbackIt);

    if (reply->error() == QNetworkReply::NoError) {
        callback(reply->readAll());
    } else if (reply->error() == QNetworkReply::AuthenticationRequiredError) {
        // 401 — 尝试刷新 Token
        if (d->refreshCallback && !d->isRefreshing) {
            d->isRefreshing = true;
            QString newToken = d->refreshCallback();
            d->bearerToken = newToken;
            d->isRefreshing = false;
            // 重试会在 refreshCallback 里处理
        }
        emit error(QStringLiteral("认证失败: %1").arg(reply->errorString()));
        callback(QByteArray());
    } else {
        emit error(reply->errorString());
        callback(QByteArray());
    }

    reply->deleteLater();
}

} // namespace QtAuthNet
