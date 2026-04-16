#include <QtAuthNet/qtauthnet_client.h>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtCore/QJsonDocument>

namespace QtAuthNet {

class Client::Private {
public:
    QString baseUrl;
    QUrl resolvePath(const QString& path) const {
        QString full = baseUrl;
        if (!full.endsWith('/') && !path.startsWith('/')) full += '/';
        else if (full.endsWith('/') && path.startsWith('/')) full.chop(1);
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

void Client::setTokenRefreshCallback(RefreshCallback callback) {
    d->refreshCallback = callback;
}

QUrl Client::resolvePath(const QString& path) const {
    return d->resolvePath(path);
}

void Client::cancel() {
    const auto replies = d->nam->findChildren<QNetworkReply*>();
    for (QNetworkReply* r : replies) r->abort();
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
    executeRequest(QStringLiteral("POST"), path, doc.toJson(QJsonDocument::Compact), callback,
                   QStringLiteral("application/json"));
}

void Client::put(const QString& path, const QByteArray& body,
                 const std::function<void(const QByteArray&)>& callback) {
    executeRequest(QStringLiteral("PUT"), path, body, callback);
}

void Client::deleteResource(const QString& path,
                             const std::function<void(const QByteArray&)>& callback) {
    executeRequest(QStringLiteral("DELETE"), path, QByteArray(), callback);
}

// ── 核心改动：不用 QVariant 存 callback，直接用 lambda ──
void Client::executeRequest(const QString& method, const QString& path,
                            const QByteArray& body,
                            const std::function<void(const QByteArray&)>& callback,
                            const QString& contentType) {
    QUrl url = d->resolvePath(path);

    if (!d->apiKey.isEmpty() && d->apiKeyLocation == QStringLiteral("query")) {
        QUrlQuery q(url); q.addQueryItem(d->apiKeyName, d->apiKey); url.setQuery(q);
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("QtAuthNet/1.0"));

    if (!d->apiKey.isEmpty() && d->apiKeyLocation == QStringLiteral("header"))
        request.setRawHeader(d->apiKeyName.toUtf8(), d->apiKey.toUtf8());
    if (!d->bearerToken.isEmpty())
        request.setRawHeader("Authorization", "Bearer " + d->bearerToken.toUtf8());
    for (auto it = d->customHeaders.constBegin(); it != d->customHeaders.constEnd(); ++it)
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    if (!body.isEmpty())
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                         contentType.isEmpty() ? QStringLiteral("application/x-www-form-urlencoded") : contentType);

    QNetworkReply* reply = nullptr;
    if (method == QStringLiteral("GET")) reply = d->nam->get(request);
    else if (method == QStringLiteral("POST")) reply = d->nam->post(request, body);
    else if (method == QStringLiteral("PUT")) reply = d->nam->put(request, body);
    else if (method == QStringLiteral("DELETE")) reply = d->nam->deleteResource(request);
    if (!reply) return;

    // 直接用 lambda 捕获 callback，完全绕开 QVariant / metatype
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        if (reply->error() == QNetworkReply::NoError) {
            callback(reply->readAll());
        } else if (reply->error() == QNetworkReply::AuthenticationRequiredError) {
            if (d->refreshCallback && !d->isRefreshing) {
                d->isRefreshing = true;
                d->bearerToken = d->refreshCallback();
                d->isRefreshing = false;
            }
            emit error(QStringLiteral("认证失败: %1").arg(reply->errorString()));
            callback(QByteArray());
        } else {
            emit error(reply->errorString());
            callback(QByteArray());
        }
        reply->deleteLater();
    });
}

void Client::error(const QString&) {} // 占位，满足 linker

} // namespace QtAuthNet
