#include <QtAuthNet/BrowserCasSession.h>
#include <QtAuthNet/HttpResponse.h>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QEventLoop>
#include <QtGui/QDesktopServices>  // 打开浏览器
#include <QtCore/QByteArray>
#include <QtCore/QLoggingCategory>

Q_LOGGING_CATEGORY(casBrowser, "qtauthnet.browser")

namespace {
constexpr int DEFAULT_TIMEOUT_MS = 300000; // 5 分钟
} // namespace

namespace QtAuthNet {

BrowserCasSession::BrowserCasSession(const QString& casUrl, QObject* parent)
    : QObject(parent)
    , m_casUrl(casUrl)
    , m_tcpServer(new QTcpServer(this))
    , m_timeoutTimer(new QTimer(this))
    , m_currentSocket(nullptr)
{
// 转发 CasSession 回调
    m_casSession = new CasSession(casUrl, this);
    m_casSession->setStatusCallback([this](bool loggedIn) {
        onCasLoginStatusChanged(loggedIn);
    });
    m_casSession->setErrorCallback([this](const QString& msg) {
        onCasError(msg);
    });

    // TCP 连接信号
    connect(m_tcpServer, &QTcpServer::newConnection,
            this, &BrowserCasSession::onNewConnection);

    // 超时定时器（单次，不循环）
    m_timeoutTimer->setSingleShot(true);
    connect(m_timeoutTimer, &QTimer::timeout, this, [this]() {
        if (m_browserLoginActive) {
            qCCritical(casBrowser) << "Browser login timeout, cancelling";
            closeBrowserLogin();
            emit browserLoginError(QStringLiteral("登录超时（5分钟）"));
            emit browserLoginFinished(false, QString());
        }
    });
}

BrowserCasSession::~BrowserCasSession() {
    cancelBrowserLogin();
}

void BrowserCasSession::setCasUrl(const QString& url) {
    if (m_casUrl == url) return;
    m_casUrl = url;
    if (m_casSession) m_casSession->deleteLater();
    m_casSession = new CasSession(url, this);
    m_casSession->setStatusCallback([this](bool loggedIn) {
        onCasLoginStatusChanged(loggedIn);
    });
    m_casSession->setErrorCallback([this](const QString& msg) {
        onCasError(msg);
    });
    emit casUrlChanged();
}

// ── 入口：启动浏览器登录 ─────────────────────────────────

int BrowserCasSession::startBrowserLogin(const QString& callbackUrl, int localPort) {
    if (m_browserLoginActive) {
        qCDebug(casBrowser) << "Already in browser login, ignoring";
        return 0;
    }

    m_callbackUrl = callbackUrl;
    m_localPort = localPort > 0 ? localPort : 18421; // 默认端口

    // 解析 callbackUrl 中的实际端口
    QUrl url(callbackUrl);
    if (url.port() > 0) m_localPort = url.port();

    qCDebug(casBrowser) << "Starting browser login, callback:" << callbackUrl
                       << "port:" << m_localPort;

    // 启动 TCP 监听
    startTcpServer(m_localPort);

    // 打开 CAS 登录页（带 service 参数指向本地回调）
    QString casLoginUrl = buildCasLoginUrl();
    qCDebug(casBrowser) << "Opening CAS login URL:" << casLoginUrl;
    QDesktopServices::openUrl(QUrl(casLoginUrl));

    m_browserLoginActive = true;
    emit browserLoginActiveChanged(true);

    // 启动超时定时器（5 分钟）
    m_timeoutTimer->start(DEFAULT_TIMEOUT_MS);

    return m_localPort;
}

void BrowserCasSession::cancelBrowserLogin() {
    closeBrowserLogin();
    emit browserLoginFinished(false, QString());
}

void BrowserCasSession::closeBrowserLogin() {
    stopTcpServer();
    m_browserLoginActive = false;
    m_readBuffer.clear();
    if (m_timeoutTimer->isActive()) m_timeoutTimer->stop();
    if (m_currentSocket) {
        m_currentSocket->disconnect();
        m_currentSocket->deleteLater();
        m_currentSocket = nullptr;
    }
    emit browserLoginActiveChanged(false);
}

// ── 业务请求 ─────────────────────────────────────────────

void BrowserCasSession::get(const QString& path) {
    if (!m_casSession) return;
    m_casSession->get(path, [this, path](const QByteArray& data) {
        int rid = m_nextRequestId++;
        emit responseReady(rid, HttpResponse(200, data, !data.isEmpty()));
    });
}

void BrowserCasSession::post(const QString& path, const QByteArray& body) {
    if (!m_casSession) return;
    m_casSession->post(path, body, [this, path](const QByteArray& data) {
        int rid = m_nextRequestId++;
        emit responseReady(rid, HttpResponse(200, data, !data.isEmpty()));
    });
}

void BrowserCasSession::postJson(const QString& path, const QVariant& json) {
    QJsonDocument doc;
    // 尝试直接解析为 JSON
    QJsonDocument parsed = QJsonDocument::fromJson(json.toByteArray());
    if (parsed.isObject()) {
        doc = parsed;
    } else {
        // 尝试将 QVariant 作为 Map 转 JSON
        QJsonDocument mapDoc = QJsonDocument::fromVariant(json.toMap());
        if (mapDoc.isObject()) {
            doc = mapDoc;
        } else {
            // 兜底：直接发原始 JSON 字符串
            doc = QJsonDocument::fromJson(json.toByteArray());
            if (doc.isNull()) {
                // 完全兜底：包装成 { "data": json }
                QJsonObject wrapper;
                wrapper.insert(QStringLiteral("data"), QString::fromUtf8(json.toByteArray()));
                doc.setObject(wrapper);
            }
        }
    }
    post(path, doc.toJson(QJsonDocument::Compact));
}

void BrowserCasSession::logout() {
    if (m_casSession) m_casSession->logout();
    m_username.clear();
}

// ── TCP 服务器 ────────────────────────────────────────────

void BrowserCasSession::startTcpServer(int port) {
    stopTcpServer();

    // 优先绑定特定端口，回退到自动选择
    if (!m_tcpServer->listen(QHostAddress::LocalHost, static_cast<quint16>(port))) {
        if (port != 0) {
            qCDebug(casBrowser) << "Port" << port << "in use, trying random port";
            m_tcpServer->listen(QHostAddress::LocalHost, 0);
        } else {
            m_tcpServer->listen(QHostAddress::LocalHost, 0);
        }
    }

    m_localPort = m_tcpServer->serverPort();
    qCDebug(casBrowser) << "TCP server listening on port" << m_localPort;
}

void BrowserCasSession::stopTcpServer() {
    if (m_tcpServer->isListening()) {
        m_tcpServer->close();
        qCDebug(casBrowser) << "TCP server stopped";
    }
}

void BrowserCasSession::onNewConnection() {
    QTcpSocket* socket = m_tcpServer->nextPendingConnection();
    if (!socket) return;

    qCDebug(casBrowser) << "Browser connected from" << socket->peerAddress().toString();

    // 如果已有连接在处理，先关掉（浏览器可能重试）
    if (m_currentSocket && m_currentSocket != socket) {
        cleanupSocket(m_currentSocket);
    }
    m_currentSocket = socket;
    m_readBuffer.clear();

    connect(socket, &QTcpSocket::readyRead, this, &BrowserCasSession::onSocketReadyRead);
    connect(socket, &QTcpSocket::disconnected, this, &BrowserCasSession::onSocketDisconnected);
    connect(socket, &QTcpSocket::errorOccurred,
            this, &BrowserCasSession::onSocketError);
}

void BrowserCasSession::onSocketReadyRead() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket || socket != m_currentSocket) return;

    m_readBuffer.append(socket->readAll());

    // HTTP 请求以两个连续 CRLF 结束（即 \r\n\r\n）
    // 找到请求行就处理
    int headerEnd = m_readBuffer.indexOf("\r\n\r\n");
    if (headerEnd == -1) return; // 还没收完

    QByteArray headerBytes = m_readBuffer.left(headerEnd);
QStringList lines = QString::fromLatin1(headerBytes).split(QStringLiteral("\r\n"));
    if (lines.isEmpty()) return;

    QString requestLine = lines.first(); // e.g. "GET /callback?ticket=ST-xxx HTTP/1.1"
    qCDebug(casBrowser) << "Got request:" << requestLine;

    handleBrowserRequest(socket, requestLine);
}

void BrowserCasSession::onSocketDisconnected() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;
    if (socket == m_currentSocket) m_currentSocket = nullptr;
    socket->deleteLater();
}

void BrowserCasSession::onSocketError(QAbstractSocket::SocketError err) {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    qCCritical(casBrowser) << "Socket error:" << err << socket->errorString();
    if (socket == m_currentSocket) m_currentSocket = nullptr;
    cleanupSocket(socket);
}

// ── 核心：解析 ticket 并验证 ─────────────────────────────

void BrowserCasSession::handleBrowserRequest(QTcpSocket* socket,
                                            const QString& requestLine) {
    // 解析 GET /path?ticket=ST-xxx HTTP/1.1
    QStringList parts = requestLine.split(' ');
    if (parts.size() < 2) {
        sendHttpResponse(socket, 400, "Bad Request", "<h1>400 Bad Request</h1>");
        return;
    }

    QString pathWithQuery = parts[1];
    int queryStart = pathWithQuery.indexOf('?');
    QString path = queryStart >= 0 ? pathWithQuery.left(queryStart) : pathWithQuery;
    QString queryString = queryStart >= 0 ? pathWithQuery.mid(queryStart + 1) : QString();

    qCDebug(casBrowser) << "Path:" << path << "Query:" << queryString;

    // 解析 ticket 参数
    QString ticket;
    if (!queryString.isEmpty()) {
        QUrlQuery query(queryString);
        ticket = query.queryItemValue(QStringLiteral("ticket"));
    }

    if (ticket.isEmpty()) {
        // 访问了回调页面但没有 ticket（可能是用户直接打开）
        sendHttpResponse(socket, 200, "OK",
            "<html><body><h1>QtAuthNet Browser CAS Login</h1>"
            "<p>Please complete login in the opened browser window.</p>"
            "<p>You can close this tab.</p></body></html>");
        return;
    }

    // 有 ticket，先返回成功页面给浏览器（让它不要显示错误）
    sendHttpResponse(socket, 200, "OK",
        "<html><body><h1>Login Successful!</h1>"
        "<p>You can close this window and return to the application.</p>"
        "<script>window.close()</script></body></html>");

    // 停止等待
    m_timeoutTimer->stop();

    // 用 ticket 验证
    finishLoginWithTicket(ticket);
}

// CAS /p3/serviceValidate 返回 JSON 格式：
// {
//   "serviceResponse": {
//     "authenticationSuccess": {
//       "user": "username",
//       "attributes": { ... }
//     }
//   }
// }
// 或：
// {
//   "serviceResponse": {
//     "authenticationFailure": {
//       "code": "INVALID_TICKET",
//       "description": "..."
//     }
//   }
// }
void BrowserCasSession::finishLoginWithTicket(const QString& ticket) {
    QUrl validateUrl = QUrl(m_casUrl);
    QString base = validateUrl.toString();
    if (!base.endsWith('/')) base += '/';
    QString validateEndpoint = base + "p3/serviceValidate";

    QUrl url(validateEndpoint);
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("service"), m_callbackUrl);
    q.addQueryItem(QStringLiteral("ticket"), ticket);
    q.addQueryItem(QStringLiteral("format"), QStringLiteral("JSON"));
    url.setQuery(q);

    qCDebug(casBrowser) << "Validating ticket via:" << url.toString();

    // 用同步方式验证（简单，不阻塞主线程因为是在异步 socket 处理中）
    // Qt 网络请求默认异步，这里用同步方式简单处理
    QNetworkAccessManager nam;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("QtAuthNet-BrowserCAS/1.0"));

    // 用 QEventLoop 等待（简单粗暴，但验证请求很快）
    QNetworkReply* reply = nam.get(request);

    // 使用 QEventLoop 等待响应
    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    timeoutTimer.start(15000); // 15 秒超时
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(&timeoutTimer, &QTimer::timeout, &loop, [&]() {
        reply->abort();
        loop.quit();
    });
    loop.exec();

    bool success = false;
    QString validatedUsername;
    QString errorMsg;

    if (reply->error() == QNetworkReply::NoError) {
        QByteArray data = reply->readAll();
        qCDebug(casBrowser) << "Validate response:" << data;

        // 解析 JSON 响应
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QJsonObject root = doc.object();
        QJsonObject serviceResponse = root.value(QStringLiteral("serviceResponse")).toObject();

        QJsonObject authSuccess = serviceResponse.value(QStringLiteral("authenticationSuccess")).toObject();
        if (!authSuccess.isEmpty()) {
            validatedUsername = authSuccess.value(QStringLiteral("user")).toString();
            success = !validatedUsername.isEmpty();
        } else {
            QJsonObject authFailure = serviceResponse.value(QStringLiteral("authenticationFailure")).toObject();
            errorMsg = authFailure.value(QStringLiteral("description")).toString();
            if (errorMsg.isEmpty()) errorMsg = authFailure.value(QStringLiteral("code")).toString();
        }
    } else {
        errorMsg = reply->errorString();
    }

    reply->deleteLater();

    // 关闭浏览器登录流程
    closeBrowserLogin();

    if (success) {
        m_username = validatedUsername;
        qCDebug(casBrowser) << "Browser login success, username:" << validatedUsername;
        emit loginStatusChanged(true);
        emit browserLoginFinished(true, validatedUsername);
    } else {
        qCCritical(casBrowser) << "Ticket validation failed:" << errorMsg;
        emit browserLoginError(QStringLiteral("Ticket 验证失败: %1").arg(errorMsg));
        emit browserLoginFinished(false, QString());
    }
}

// ── 工具 ────────────────────────────────────────────────

QString BrowserCasSession::buildCasLoginUrl() const {
    QString base = m_casUrl;
    if (!base.endsWith('/')) base += '/';
    QUrl url(base + QStringLiteral("login"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("service"), QUrl::toPercentEncoding(m_callbackUrl));
    url.setQuery(q);
    return url.toString();
}

void BrowserCasSession::sendHttpResponse(QTcpSocket* socket, int statusCode,
                                         const QString& statusText,
                                         const QString& body,
                                         const QString& contentType) {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) return;

    QByteArray bodyBytes = body.toUtf8();
    QByteArray response =
        "HTTP/1.1 " + QByteArray::number(statusCode) + " " + statusText.toLatin1() + "\r\n"
        "Content-Type: " + (contentType.isEmpty() ? QByteArrayLiteral("text/plain") : contentType.toLatin1()) + "\r\n"
        "Content-Length: " + QByteArray::number(bodyBytes.size()) + "\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-store\r\n"
        "\r\n";

    socket->write(response);
    socket->write(bodyBytes);
    socket->flush();
    socket->disconnectFromHost();
}

void BrowserCasSession::sendRedirectResponse(QTcpSocket* socket, const QString& location) {
    sendHttpResponse(socket, 302, "Found",
        "<html><body>Redirecting...</body></html>",
        QString());
    Q_UNUSED(location)
}

void BrowserCasSession::cleanupSocket(QTcpSocket* socket) {
    if (!socket) return;
    socket->disconnect();
    socket->abort();
    if (socket == m_currentSocket) m_currentSocket = nullptr;
    socket->deleteLater();
}

// ── CasSession 信号转发 ─────────────────────────────────

void BrowserCasSession::onCasLoginStatusChanged(bool loggedIn) {
    emit loginStatusChanged(loggedIn);
}

void BrowserCasSession::onCasError(const QString& message) {
    emit error(message);
}

} // namespace QtAuthNet
