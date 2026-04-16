#pragma once
#include "HttpResponse.h"
#include <QtNetwork/QNetworkAccessManager>
#include <QtCore/QTimer>

namespace QtAuthNet {

/**
 * CasSessionQml — CasSession 的 QML 封装
 *
 * 核心转换：std::function 回调 → Qt Signal
 *
 * QML 使用示例：
 *
 *   CasSession {
 *       id: cas
 *       baseUrl: "https://cas.example.com"
 *
 *       onLoginStatusChanged: (loggedIn) => {
 *           statusLabel.text = loggedIn ? "已登录" : "未登录"
 *       }
 *       onError: (msg) => console.error("CAS 错误:", msg)
 *       onResponseReady: (reqId, resp) => {
 *           if (resp.ok) console.log(resp.json)
 *       }
 *   }
 *
 *   // 登录
 *   cas.login(usernameField.text, passwordField.text)
 *
 *   // 发起需要认证的请求
 *   cas.get("/api/protected")
 *   cas.postJson("/api/data", { "key": "value" })
 *
 *   // 登出
 *   cas.logout()
 */
class CasSessionQml : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString baseUrl READ baseUrl WRITE setBaseUrl NOTIFY baseUrlChanged)
    Q_PROPERTY(bool loggedIn READ isLoggedIn NOTIFY loginStatusChanged)
    Q_PROPERTY(QString username READ username NOTIFY loginStatusChanged)
    Q_PROPERTY(int renewIntervalSec READ renewIntervalSec WRITE setRenewIntervalSec NOTIFY renewIntervalChanged)

public:
    explicit CasSessionQml(QObject* parent = nullptr);
    ~CasSessionQml() override;

    QString baseUrl() const { return m_casUrl; }
    void setBaseUrl(const QString& url);

    bool isLoggedIn() const { return m_loggedIn; }
    QString username() const { return m_username; }

    int renewIntervalSec() const { return m_renewIntervalSec; }
    void setRenewIntervalSec(int sec);

    // ── 操作方法 ────────────────────────────────
    Q_INVOKABLE void login(const QString& username, const QString& password);
    Q_INVOKABLE void logout();
    Q_INVOKABLE void renew();

    Q_INVOKABLE void get(const QString& path);
    Q_INVOKABLE void post(const QString& path, const QByteArray& body);
    Q_INVOKABLE void postJson(const QString& path, const QVariant& json);

signals:
    // ── 登录状态 ───────────────────────────────
    void loginStatusChanged(bool loggedIn);
    void error(const QString& message);

    // ── 请求响应 ────────────────────────────────
    // requestId: 用于区分同一个信号中多个请求的来源
    void responseReady(int requestId, const QtAuthNet::HttpResponse& response);

    // ── 内部状态 ────────────────────────────────
    void baseUrlChanged();
    void renewIntervalChanged();

private:
    void setBusy(bool on);
    Q_DISABLE_COPY(CasSessionQml)

private slots:
    void onRenewTimeout();

private:
    void acquireServiceTicket(const QString& serviceUrl);
    void doServiceGet(const QString& path, const QString& st);
    void doServicePost(const QString& path, const QString& st, const QByteArray& body);

    QString buildCasUrl(const QString& path = QString()) const;

    QString m_casUrl;
    QString m_tgt;
    QString m_username;
    bool m_loggedIn = false;
    int m_renewIntervalSec = 3600;

    QNetworkAccessManager* m_nam = nullptr;
    QTimer* m_renewTimer = nullptr;

    // 待处理 ST 获取后的请求队列
    struct StRequest {
        int id;
        QString path;
        QByteArray body;
        bool isPost;
    };
    QList<StRequest> m_pendingStRequests;
    int m_nextRequestId = 1;
};

} // namespace QtAuthNet
