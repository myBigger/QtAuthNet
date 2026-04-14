#include "qtauthnet_client.h"
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <QtCore/QTimer>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QUrlQuery>

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
    QString apiKeyLocation = QStringLiteral("header");
    QString apiKeyName = QStringLiteral("X-API-Key");
    QMap<QString, QString> customHeaders;
    RefreshCallback refreshCallback;
    QList<QPair<QNetworkReply*, std::function<void(const QByteArray&)>>> pendingCallbacks;
    bool isRefreshing = false;
};

Client::Client(const QString& baseUrl, QObject* parent)
    : QObject(parent), d(new Private)
{
    d->baseUrl = baseUrl;
    d->nam = new QNetworkAccessManager(this);
}

Client::~Client() = default;

void Client::setBearerToken(const QString& token) { d->bearerToken = token; }
void Client::setBasicAuth(const QString& username, const QString& password) {
    d->basicUser = username; d->basicPass = password;
}
void Client::setApiKey(const QString& key, const QString& location) {
    d->apiKey = key; d->apiKeyLocation = location;
}
void Client::setHeader(const QString& key, const QString& value) {
    d->customHeaders[key] = value;
}
void Client::setTokenRefreshCallback(RefreshCallback callback) { d->refreshCallback = callback; }

void Client::cancel() {
    for (auto& pair : d->pendingCallbacks) {
        if (pair.first && !pair.first->isFinished()) {
            pair.first->abort();
        }
    }
    d->pendingCallbacks.clear();
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
    executeRequest(QStringLiteral("POST"), path, doc.toJson(QJsonDocument::Compact), callback, QStringLiteral("application/json"));
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
                             const std::function<void(const QByteArray&)>& callback,
                             const QString& contentType) {
    QUrl url = d->resolvePath(path);

    if (!d->apiKey.isEmpty() && d->apiKeyLocation == QStringLiteral("query")) {
        QUrlQuery query(url);
        query.addQueryItem(d->apiKeyName, d->apiKey);
        url.setQuery(query);
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("QtAuthNet/1.0"));

    if (!d->apiKey.isEmpty() && d->apiKeyLocation == QStringLiteral("header")) {
        request.setRawHeader(d->apiKeyName.toUtf8(), d->apiKey.toUtf8());
    }
    if (!d->bearerToken.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + d->bearerToken.toUtf8());
    }
    for (auto it = d->customHeaders.constBegin(); it != d->customHeaders.constEnd(); ++it) {
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }
    if (!body.isEmpty()) {
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          contentType.isEmpty() ? QStringLiteral("application/x-www-form-urlencoded") : contentType);
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

    d->pendingCallbacks.append({reply, callback});
    connect(reply, &QNetworkReply::finished, this, &Client::onReplyFinished);
}

void Client::onReplyFinished() {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    std::function<void(const QByteArray&)> callback;
    for (auto& pair : d->pendingCallbacks) {
        if (pair.first == reply) { callback = pair.second; break; }
    }
    d->pendingCallbacks.removeAll({reply, callback});

    if (reply->error() == QNetworkReply::NoError) {
        callback(reply->readAll());
    } else if (reply->error() == QNetworkReply::AuthenticationRequiredError) {
        if (d->refreshCallback && !d->isRefreshing) {
            d->isRefreshing = true;
            QString newToken = d->refreshCallback();
            d->bearerToken = newToken;
            d->isRefreshing = false;
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
