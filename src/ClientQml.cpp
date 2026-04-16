#include <QtAuthNet/ClientQml.h>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtCore/QJsonDocument>
#include <QtCore/QTimer>

namespace QtAuthNet {

ClientQml::ClientQml(QObject* parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this))
{}

ClientQml::~ClientQml() {
    cancel();
}

// ── 属性设置 ──────────────────────────────────────

void ClientQml::setBaseUrl(const QString& url) {
    if (m_baseUrl == url) return;
    m_baseUrl = url;
    emit baseUrlChanged();
}

void ClientQml::setBearerToken(const QString& token) {
    if (m_bearerToken == token) return;
    m_bearerToken = token;
    emit bearerTokenChanged();
}

void ClientQml::setBasicUser(const QString& user) {
    if (m_basicUser == user) return;
    m_basicUser = user;
    emit authChanged();
}

void ClientQml::setBasicPass(const QString& pass) {
    if (m_basicPass == pass) return;
    m_basicPass = pass;
    emit authChanged();
}

void ClientQml::setApiKey(const QString& key) {
    if (m_apiKey == key) return;
    m_apiKey = key;
    emit authChanged();
}

void ClientQml::setApiKeyLocation(const QString& loc) {
    if (m_apiKeyLocation == loc) return;
    m_apiKeyLocation = loc;
    emit authChanged();
}

void ClientQml::setTimeout(int ms) {
    if (m_timeoutMs == ms) return;
    m_timeoutMs = ms;
    emit timeoutChanged();
}

void ClientQml::setTokenRefreshHandler(QObject* receiver, const QString& slotName) {
    m_tokenRefreshReceiver = receiver;
    m_tokenRefreshSlot = slotName;
}

void ClientQml::setBusy(bool on) {
    if (on) {
        ++m_busyCount;
    } else {
        --m_busyCount;
        if (m_busyCount < 0) m_busyCount = 0;
    }
    bool wasLoading = m_loading;
    m_loading = m_busyCount > 0;
    if (wasLoading != m_loading) emit loadingChanged(m_loading);
    emit busyChanged(isBusy());
}

// ── Token 刷新 ────────────────────────────────────

void ClientQml::refreshToken(const QString& newToken) {
    m_bearerToken = newToken;
    m_isRefreshing = false;

    // 重试所有等待中的请求
    auto pending = m_pendingWhileRefreshing;
    m_pendingWhileRefreshing.clear();
    for (const auto& p : pending) {
        doRequest(p.method, p.path, p.body, p.contentType);
    }
}

// ── 请求入口 ──────────────────────────────────────

void ClientQml::get(const QString& path) {
    doRequest(QStringLiteral("GET"), path, QByteArray(), QString());
}

void ClientQml::post(const QString& path, const QByteArray& body,
                     const QString& contentType) {
    doRequest(QStringLiteral("POST"), path, body,
              contentType.isEmpty() ? QStringLiteral("application/x-www-form-urlencoded") : contentType);
}

void ClientQml::postJson(const QString& path, const QVariant& json) {
    QJsonDocument doc;
    if (json.canConvert<QJsonDocument>()) {
        doc = json.value<QJsonDocument>();
    } else if (json.canConvert<QJsonObject>()) {
        doc.setObject(json.value<QJsonObject>());
    } else if (json.canConvert<QJsonArray>()) {
        doc.setArray(json.value<QJsonArray>());
    } else {
        // 尝试从 QVariantMap 转换
        if (json.type() == QVariant::Map) {
            QJsonObject obj;
            QVariantMap map = json.toMap();
            for (auto it = map.constBegin(); it != map.constEnd(); ++it)
                obj[it.key()] = QJsonValue::fromVariant(it.value());
            doc.setObject(obj);
        } else {
            doc = QJsonDocument::fromJson(json.toByteArray());
        }
    }
    doRequest(QStringLiteral("POST"), path, doc.toJson(QJsonDocument::Compact),
              QStringLiteral("application/json"));
}

void ClientQml::put(const QString& path, const QByteArray& body,
                    const QString& contentType) {
    doRequest(QStringLiteral("PUT"), path, body,
              contentType.isEmpty() ? QStringLiteral("application/x-www-form-urlencoded") : contentType);
}

void ClientQml::del(const QString& path) {
    doRequest(QStringLiteral("DELETE"), path, QByteArray(), QString());
}

void ClientQml::request(const QString& method, const QString& path,
                        const QVariant& body, const QString& contentType) {
    QByteArray bodyBytes;
    QString finalContentType = contentType;

    if (body.isValid() && !body.isNull()) {
        if (body.type() == QVariant::ByteArray) {
            bodyBytes = body.toByteArray();
        } else {
            QJsonDocument doc;
            if (body.canConvert<QJsonDocument>()) {
                doc = body.value<QJsonDocument>();
            } else if (body.canConvert<QJsonObject>()) {
                doc.setObject(body.value<QJsonObject>());
            } else {
                doc = QJsonDocument::fromJson(body.toByteArray());
            }
            bodyBytes = doc.toJson(QJsonDocument::Compact);
            if (finalContentType.isEmpty())
                finalContentType = QStringLiteral("application/json");
        }
    }

    if (finalContentType.isEmpty())
        finalContentType = QStringLiteral("application/x-www-form-urlencoded");

    doRequest(method.toUpper(), path, bodyBytes, finalContentType);
}

void ClientQml::cancel() {
    const auto replies = m_nam->findChildren<QNetworkReply*>();
    for (QNetworkReply* r : replies)
        r->abort();
    m_pendingWhileRefreshing.clear();
}

// ── 核心请求逻辑 ───────────────────────────────────

void ClientQml::doRequest(const QString& method, const QString& path,
                          const QByteArray& body, const QString& contentType) {
    if (m_baseUrl.isEmpty()) {
        emit error(QStringLiteral("baseUrl 未设置"));
        return;
    }

    // 解析 URL
    QString fullUrl = m_baseUrl;
    if (!fullUrl.endsWith('/') && !path.startsWith('/'))
        fullUrl += '/';
    else if (fullUrl.endsWith('/') && path.startsWith('/'))
        fullUrl.chop(1);
    fullUrl += path;

    QUrl url(fullUrl);
    QUrlQuery query;

    // API Key 放到 query 参数
    if (!m_apiKey.isEmpty() && m_apiKeyLocation == QStringLiteral("query")) {
        query.addQueryItem(QStringLiteral("apikey"), m_apiKey);
        url.setQuery(query);
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("QtAuthNet/1.0"));
    if (m_timeoutMs > 0)
        request.setTransferTimeout(m_timeoutMs);

    // Auth Header
    if (!m_apiKey.isEmpty() && m_apiKeyLocation == QStringLiteral("header"))
        request.setRawHeader("X-API-Key", m_apiKey.toUtf8());
    if (!m_bearerToken.isEmpty())
        request.setRawHeader("Authorization", "Bearer " + m_bearerToken.toUtf8());
    if (!m_basicUser.isEmpty() && !m_basicPass.isEmpty()) {
        QString cred = m_basicUser + ":" + m_basicPass;
        request.setRawHeader("Authorization",
                             "Basic " + cred.toUtf8().toBase64());
    }

    // Body Content-Type
    if (!body.isEmpty())
        request.setHeader(QNetworkRequest::ContentTypeHeader, contentType);

    // 发送请求
    QNetworkReply* reply = nullptr;
    if (method == QLatin1String("GET")) {
        reply = m_nam->get(request);
    } else if (method == QLatin1String("POST")) {
        reply = m_nam->post(request, body);
    } else if (method == QLatin1String("PUT")) {
        reply = m_nam->put(request, body);
    } else if (method == QLatin1String("DELETE")) {
        reply = m_nam->deleteResource(request);
    }

    if (!reply) {
        emit error(QStringLiteral("未知 HTTP 方法: %1").arg(method));
        return;
    }

    int reqId = m_nextRequestId++;
    setBusy(true);

    // 把请求信息存在 reply 上
    reply->setProperty("_qmlReqId", reqId);
    reply->setProperty("_qmlMethod", method);
    reply->setProperty("_qmlPath", path);
    reply->setProperty("_qmlBody", body);
    reply->setProperty("_qmlContentType", contentType);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        int reqId = reply->property("_qmlReqId").toInt();
        QString method = reply->property("_qmlMethod").toString();
        QString path = reply->property("_qmlPath").toString();
        QByteArray body = reply->property("_qmlBody").toByteArray();
        QString contentType = reply->property("_qmlContentType").toString();

        setBusy(false);

        QNetworkReply::NetworkError err = reply->error();
        QByteArray data = reply->readAll();
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        reply->deleteLater();

        if (err == QNetworkReply::NoError) {
            emit responseReady(reqId, HttpResponse(statusCode, data, true));

        } else if (err == QNetworkReply::AuthenticationRequiredError) {
            // 401 — 尝试刷新 token
            if (!m_tokenRefreshReceiver && m_tokenRefreshSlot.isEmpty()) {
                // 没有配置刷新回调，直接报错
                emit error(QStringLiteral("认证失败 (401)，请设置 tokenRefreshHandler"));
                emit responseReady(reqId, HttpResponse(statusCode, data, false));
                return;
            }

            // 已经在刷新中，把请求加入待重试队列
            if (m_isRefreshing) {
                m_pendingWhileRefreshing.append({reqId, method, path, body, contentType});
                return;
            }

            m_isRefreshing = true;
            emit tokenRefreshRequested();

            // QML 侧应监听 tokenRefreshRequested，然后调用 refreshToken()
            // 如果 QML 侧没有调用，5 秒后自动超时，避免永久卡住
            QTimer::singleShot(5000, this, [this, reqId, statusCode, data]() {
                if (m_isRefreshing) {
                    m_isRefreshing = false;
                    emit error(QStringLiteral("Token 刷新超时"));
                    emit responseReady(reqId, HttpResponse(statusCode, data, false));
                }
            });

        } else {
            QString msg = QStringLiteral("HTTP %1: %2").arg(statusCode).arg(reply->errorString());
            emit error(msg);
            emit responseReady(reqId, HttpResponse(statusCode, data, false));
        }
    });
}

} // namespace QtAuthNet
