#pragma once
#include "HttpResponse.h"
#include <QtNetwork/QNetworkAccessManager>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>

namespace QtAuthNet {

/**
 * ClientQml — Client 的 QML 封装
 *
 * 核心转换：std::function 回调 → Qt Signal
 *
 * QML 使用示例：
 *
 *   Client {
 *       id: api
 *       baseUrl: "https://api.example.com"
 *       bearerToken: tokenField.text
 *
 *       onResponseReady: (resp) => {
 *           if (resp.ok) console.log(resp.json)
 *           else console.error(resp.errorMessage())
 *       }
 *       onError: (msg) => console.error("网络错误:", msg)
 *   }
 *
 *   // 发起请求（所有方法均返回 void，结果通过 signal 回调）
 *   api.get("/users/me")
 *   api.postJson("/users", { "name": "bigege" })
 *   api.post("/upload", byteArray, "application/octet-stream")
 *   api.put("/users/1", "{...}")
 *   api.deleteResource("/users/1")
 */
class ClientQml : public QObject {
    Q_OBJECT

    // ── QML 可读写属性 ──────────────────────────
    Q_PROPERTY(QString baseUrl READ baseUrl WRITE setBaseUrl NOTIFY baseUrlChanged)
    Q_PROPERTY(QString bearerToken READ bearerToken WRITE setBearerToken NOTIFY bearerTokenChanged)
    Q_PROPERTY(QString basicUser READ basicUser WRITE setBasicUser NOTIFY authChanged)
    Q_PROPERTY(QString basicPass READ basicPass WRITE setBasicPass NOTIFY authChanged)
    Q_PROPERTY(QString apiKey READ apiKey WRITE setApiKey NOTIFY authChanged)
    Q_PROPERTY(QString apiKeyLocation READ apiKeyLocation WRITE setApiKeyLocation NOTIFY authChanged)
    Q_PROPERTY(int timeout READ timeout WRITE setTimeout NOTIFY timeoutChanged)
    Q_PROPERTY(bool loading READ isLoading NOTIFY loadingChanged)

    // ── QML 不可写的只读属性 ────────────────────
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)

public:
    explicit ClientQml(QObject* parent = nullptr);
    ~ClientQml() override;

    // ── 属性读写 ────────────────────────────────
    QString baseUrl() const { return m_baseUrl; }
    void setBaseUrl(const QString& url);

    QString bearerToken() const { return m_bearerToken; }
    void setBearerToken(const QString& token);

    QString basicUser() const { return m_basicUser; }
    void setBasicUser(const QString& user);
    QString basicPass() const { return m_basicPass; }
    void setBasicPass(const QString& pass);

    QString apiKey() const { return m_apiKey; }
    void setApiKey(const QString& key);
    QString apiKeyLocation() const { return m_apiKeyLocation; }
    void setApiKeyLocation(const QString& loc); // "header" or "query"

    int timeout() const { return m_timeoutMs; }
    void setTimeout(int ms);

    bool isLoading() const { return m_loading; }

    bool isBusy() const { return m_busyCount > 0; }

    // ── 请求方法（全部 Q_INVOKABLE，结果在 signal 中返回）─
    Q_INVOKABLE void get(const QString& path);
    Q_INVOKABLE void post(const QString& path, const QByteArray& body,
                          const QString& contentType = QStringLiteral("application/x-www-form-urlencoded"));
    Q_INVOKABLE void postJson(const QString& path, const QVariant& json);
    Q_INVOKABLE void put(const QString& path, const QByteArray& body,
                         const QString& contentType = QStringLiteral("application/x-www-form-urlencoded"));
    Q_INVOKABLE void del(const QString& path);
    Q_INVOKABLE void request(const QString& method, const QString& path,
                             const QVariant& body = QVariant(),
                             const QString& contentType = QString());

    // ── Token 刷新（QML 侧实现刷新逻辑）─────────
    // QML 中设置回调：
    //   api.tokenRefreshRequested.connect(refreshTokenFunction)
    Q_INVOKABLE void setTokenRefreshHandler(QObject* receiver, const QString& slotName);

    // ── 取消所有请求 ────────────────────────────
    Q_INVOKABLE void cancel();

signals:
    // ── 响应信号（QML 中监听此信号获取结果）────
    // 用 QList 而不是单个对象，因为 request() 可能返回多个？
    // 实际上每个请求一个 id，对应一个 responseReady
    void responseReady(int requestId, const QtAuthNet::HttpResponse& response);

    // ── 错误信号 ───────────────────────────────
    void error(const QString& message);

    // ── 状态信号 ───────────────────────────────
    void loadingChanged(bool loading);
    void busyChanged(bool busy);

    // ── Token 刷新请求信号 ─────────────────────
    // 收到 401 时发出，QML 侧监听并调用 refreshToken() 告知新 token
    void tokenRefreshRequested();

    // ── 属性变更信号 ───────────────────────────
    void baseUrlChanged();
    void bearerTokenChanged();
    void authChanged();
    void timeoutChanged();

public slots:
    // ── Token 刷新完成后调用此方法 ─────────────
    // param newToken: QML 侧获取到的新 token
    void refreshToken(const QString& newToken);

private:
    void setBusy(bool on);
    void doRequest(const QString& method, const QString& path,
                   const QByteArray& body, const QString& contentType);

    QString m_baseUrl;
    QString m_bearerToken;
    QString m_basicUser;
    QString m_basicPass;
    QString m_apiKey;
    QString m_apiKeyLocation = QStringLiteral("header");
    int m_timeoutMs = 30000;

    bool m_loading = false;
    int m_busyCount = 0;

    QNetworkAccessManager* m_nam = nullptr;
    QObject* m_tokenRefreshReceiver = nullptr;
    QString m_tokenRefreshSlot;
    bool m_isRefreshing = false;
    int m_nextRequestId = 1;

    struct PendingRequest {
        int id;
        QString method;
        QString path;
        QByteArray body;
        QString contentType;
    };
    QList<PendingRequest> m_pendingWhileRefreshing;
};

} // namespace QtAuthNet
