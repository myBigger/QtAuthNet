#pragma once
#include <QtAuthNet/qtauthnet_session.h>
#include <QtAuthNet/HttpResponse.h>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QtCore/QTimer>

namespace QtAuthNet {

/**
 * BrowserCasSession — 通过浏览器完成 CAS 登录
 *
 * 流程：启动本地 TCP 服务器 → 打开系统浏览器 → 拦截回调 → 解析 ticket → 验证
 *
 * QML 用法：
 *
 *   BrowserCasSession {
 *       id: cas
 *       casUrl: "https://cas.example.com"
 *
 *       onBrowserLoginFinished: (success, username) => {
 *           console.log("Login:", success, username)
 *       }
 *   }
 *
 *   Button {
 *       text: "登录"
 *       onClicked: cas.startBrowserLogin("http://localhost:19876/callback", 19876)
 *   }
 */
class BrowserCasSession : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString casUrl READ casUrl WRITE setCasUrl NOTIFY casUrlChanged)
    Q_PROPERTY(bool browserLoginActive READ isBrowserLoginActive NOTIFY browserLoginActiveChanged)
    Q_PROPERTY(bool loggedIn READ isLoggedIn NOTIFY loginStatusChanged)
    Q_PROPERTY(QString username READ username NOTIFY loginStatusChanged)

public:
    /**
     * @param casUrl       CAS 服务器基础地址（不含 /login 等路径）
     * @param parent
     */
    Q_INVOKABLE explicit BrowserCasSession(const QString& casUrl = QString(),
                                           QObject* parent = nullptr);
    ~BrowserCasSession() override;

    QString casUrl() const { return m_casUrl; }
    void setCasUrl(const QString& url);

    bool isBrowserLoginActive() const { return m_browserLoginActive; }
    bool isLoggedIn() const { return m_casSession && m_casSession->isLoggedIn(); }
    QString username() const { return m_username; }

    // ── 核心方法 ────────────────────────────────
    /**
     * 启动浏览器登录流程
     *
     * @param callbackUrl    回调地址（必须是 http://localhost:xxxx 格式）
     *                        建议格式：http://localhost:{port}/callback
     * @param localPort      本地 TCP 监听端口（0 = 自动选择空闲端口）
     *                        传入 callbackUrl 中包含的端口号
     * @return              实际监听的端口号（0 表示失败）
     */
    Q_INVOKABLE int startBrowserLogin(const QString& callbackUrl, int localPort = 0);

    /**
     * 取消正在进行的浏览器登录
     */
    Q_INVOKABLE void cancelBrowserLogin();

    // ── 代理请求（通过 CAS ST）────────────────────
    Q_INVOKABLE void get(const QString& path);
    Q_INVOKABLE void post(const QString& path, const QByteArray& body);
    Q_INVOKABLE void postJson(const QString& path, const QVariant& json);

    // ── 登出 ────────────────────────────────────
    Q_INVOKABLE void logout();

signals:
    // ── 浏览器登录状态 ─────────────────────────
    /**
     * 浏览器登录流程结束（成功或失败）
     * @param success  是否成功
     * @param username 成功时为 CAS 返回的用户名，失败时为空
     */
    void browserLoginFinished(bool success, const QString& username);

    void browserLoginActiveChanged(bool active);
    void browserLoginError(const QString& message);
    void casUrlChanged();

    // ── 代理信号 ───────────────────────────────
    void loginStatusChanged(bool loggedIn);
    void error(const QString& message);

    // requestId: 用于区分同一个信号中多个请求的来源
    void responseReady(int requestId, const QtAuthNet::HttpResponse& response);

private slots:
    void onCasLoginStatusChanged(bool loggedIn);
    void onCasError(const QString& message);
    void onSocketReadyRead();
    void onSocketDisconnected();
    void onSocketError(QAbstractSocket::SocketError err);

private:
    // ── TCP 服务器 ───────────────────────────────
    void startTcpServer(int port);
    void stopTcpServer();
    void onNewConnection();
    void closeBrowserLogin();
    void handleBrowserRequest(QTcpSocket* socket, const QString& requestLine);
    void finishLoginWithTicket(const QString& ticket);
    void sendHttpResponse(QTcpSocket* socket, int statusCode,
                           const QString& statusText,
                           const QString& body,
                           const QString& contentType = QString());
    void sendRedirectResponse(QTcpSocket* socket, const QString& location);
    void cleanupSocket(QTcpSocket* socket);
    QString buildCasLoginUrl() const;

    // ── 状态 ───────────────────────────────────
    QString m_casUrl;
    QString m_username;
    QString m_callbackUrl;
    bool m_browserLoginActive = false;
    int m_localPort = 0;
    bool m_browserLoginHandled = false;

    // 静态常量
    static constexpr int DEFAULT_TIMEOUT_MS = 300000; // 5 分钟

    QTcpServer* m_tcpServer = nullptr;
    QTimer* m_timeoutTimer = nullptr;
    QByteArray m_readBuffer;
    QTcpSocket* m_currentSocket = nullptr;
    CasSession* m_casSession = nullptr;
    int m_nextRequestId = 1;
};

} // namespace QtAuthNet
