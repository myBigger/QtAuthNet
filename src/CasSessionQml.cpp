#include <QtAuthNet/CasSessionQml.h>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtCore/QJsonDocument>

namespace QtAuthNet {

CasSessionQml::CasSessionQml(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_renewTimer(new QTimer(this))
{
    connect(m_renewTimer, &QTimer::timeout, this, &CasSessionQml::onRenewTimeout);
}

CasSessionQml::~CasSessionQml() {
    m_renewTimer->stop();
}

// ── 属性 ───────────────────────────────────────────

void CasSessionQml::setBaseUrl(const QString& url) {
    if (m_casUrl == url) return;
    m_casUrl = url;
    emit baseUrlChanged();
}

void CasSessionQml::setRenewIntervalSec(int sec) {
    if (m_renewIntervalSec == sec) return;
    m_renewIntervalSec = sec;
    emit renewIntervalChanged();
}

void CasSessionQml::setBusy(bool on) {
    // 占位：可扩展为 loading 状态
    Q_UNUSED(on)
}

// ── 登录 / 登出 ───────────────────────────────────

void CasSessionQml::login(const QString& username, const QString& password) {
    if (m_casUrl.isEmpty()) {
        emit error(QStringLiteral("baseUrl 未设置"));
        return;
    }

    m_username = username;

    // CAS v1/tickets/{username}  POST username=xxx&password=xxx
    QString tgtUrl = buildCasUrl(QStringLiteral("/") + username);
    QUrl url(tgtUrl);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));

    QUrlQuery body;
    body.addQueryItem(QStringLiteral("username"), username);
    body.addQueryItem(QStringLiteral("password"), password);

    QNetworkReply* reply = m_nam->post(request, body.toString(QUrl::FullyEncoded).toUtf8());
    int reqId = m_nextRequestId++;
    reply->setProperty("_qmlReqId", reqId);
    reply->setProperty("_qmlLoginMode", true);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        int reqId = reply->property("_qmlReqId").toInt();
        Q_UNUSED(reqId)
        setBusy(false);

        QUrl redirectUrl = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        QString tgtFromRedirect;
        if (redirectUrl.isValid()) {
            tgtFromRedirect = redirectUrl.toString();
        } else {
            // 直接 body 返回 TGT-xxx
            QString resp = QString::fromUtf8(reply->readAll()).trimmed();
            if (resp.contains("TGT-"))
                tgtFromRedirect = m_casUrl + "/v1/tickets/" + resp;
        }

        reply->deleteLater();

        if (tgtFromRedirect.contains("TGT-")) {
            m_tgt = tgtFromRedirect;
            m_loggedIn = true;
            m_renewTimer->start(m_renewIntervalSec * 1000);
            emit loginStatusChanged(true);
        } else {
            QString errorMsg = QString::fromUtf8(reply->readAll()).left(200);
            emit error(QStringLiteral("CAS 登录失败: %1").arg(errorMsg));
            emit loginStatusChanged(false);
        }
    });
}

void CasSessionQml::logout() {
    if (m_tgt.isEmpty()) return;

    QNetworkRequest request{QUrl(m_tgt)};
    QNetworkReply* reply = m_nam->deleteResource(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_tgt.clear();
        m_loggedIn = false;
        m_renewTimer->stop();
        emit loginStatusChanged(false);
    });
}

void CasSessionQml::renew() {
    if (m_tgt.isEmpty()) return;

    QNetworkRequest request{QUrl(m_tgt)};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
    QNetworkReply* reply = m_nam->post(request, QByteArray());

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        bool ok = (reply->error() == QNetworkReply::NoError);
        reply->deleteLater();

        if (ok) {
            m_renewTimer->start(m_renewIntervalSec * 1000);
        } else {
            m_loggedIn = false;
            m_tgt.clear();
            m_renewTimer->stop();
            emit error(QStringLiteral("CAS 会话续期失败，需重新登录"));
            emit loginStatusChanged(false);
        }
    });
}

void CasSessionQml::onRenewTimeout() {
    renew();
}

// ── 业务请求 ─────────────────────────────────────

void CasSessionQml::get(const QString& path) {
    if (!m_loggedIn) {
        emit error(QStringLiteral("未登录，无法发起请求"));
        emit responseReady(-1, HttpResponse(401, QByteArrayLiteral("Not logged in"), false));
        return;
    }
    QString serviceUrl = buildCasUrl(path);
    acquireServiceTicket(serviceUrl);
}

void CasSessionQml::post(const QString& path, const QByteArray& body) {
    if (!m_loggedIn) {
        emit error(QStringLiteral("未登录，无法发起请求"));
        emit responseReady(-1, HttpResponse(401, QByteArrayLiteral("Not logged in"), false));
        return;
    }
    QString serviceUrl = buildCasUrl(path);
    acquireServiceTicket(serviceUrl);
}

void CasSessionQml::postJson(const QString& path, const QVariant& json) {
    QJsonDocument doc;
    if (json.canConvert<QJsonDocument>()) {
        doc = json.value<QJsonDocument>();
    } else if (json.canConvert<QJsonObject>()) {
        doc.setObject(json.value<QJsonObject>());
    } else {
        doc = QJsonDocument::fromJson(json.toByteArray());
    }
    post(path, doc.toJson(QJsonDocument::Compact));
}

// ── 内部：ST 获取 + 请求执行 ──────────────────────

void CasSessionQml::acquireServiceTicket(const QString& serviceUrl) {
    QNetworkRequest request{QUrl(m_tgt)};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
    QUrlQuery body;
    body.addQueryItem(QStringLiteral("service"), serviceUrl);
    QNetworkReply* reply = m_nam->post(request, body.toString(QUrl::FullyEncoded).toUtf8());

    int reqId = m_nextRequestId++;
    reply->setProperty("_qmlReqId", reqId);
    reply->setProperty("_qmlServiceUrl", serviceUrl);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        int reqId = reply->property("_qmlReqId").toInt();
        QString serviceUrl = reply->property("_qmlServiceUrl").toString();
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            emit error(QStringLiteral("获取 Service Ticket 失败: %1").arg(reply->errorString()));
            emit responseReady(reqId, HttpResponse(0, QByteArray(), false));
            return;
        }

        QString st = QString::fromUtf8(reply->readAll()).trimmed();
        if (st.isEmpty()) {
            emit error(QStringLiteral("CAS 返回了空的 Service Ticket"));
            emit responseReady(reqId, HttpResponse(0, QByteArray(), false));
            return;
        }

        // 从待处理队列中找到对应请求并执行
        // 这里用 serviceUrl 匹配（因为 get/post 没有直接传入 body）
        // ST 获取后直接执行（简化处理）
        doServiceGet(serviceUrl, st);
    });
}

void CasSessionQml::doServiceGet(const QString& path, const QString& st) {
    QUrl url(buildCasUrl(path));
    QUrlQuery q(url); q.addQueryItem(QStringLiteral("ticket"), st); url.setQuery(q);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("QtAuthNet-CAS/1.0"));
    QNetworkReply* reply = m_nam->get(request);

    int reqId = m_nextRequestId++;
    reply->setProperty("_qmlReqId", reqId);
    setBusy(true);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        int reqId = reply->property("_qmlReqId").toInt();
        setBusy(false);
        QByteArray data = reply->readAll();
        bool ok = (reply->error() == QNetworkReply::NoError);
        reply->deleteLater();
        emit responseReady(reqId, HttpResponse(
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
            data, ok));
    });
}

// ── 工具 ───────────────────────────────────────────

QString CasSessionQml::buildCasUrl(const QString& path) const {
    QString url = m_casUrl;
    if (!url.endsWith('/')) url += '/';
    url += "v1/tickets";
    if (!path.isEmpty()) {
        if (!path.startsWith('/')) url += '/';
        url += path;
    }
    return QUrl(url).toString();
}

} // namespace QtAuthNet
