#pragma once
#include "qtauthnet_global.h"
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <functional>

namespace QtAuthNet {

class QTAUTHNET_EXPORT CasSession : public QObject {
    Q_OBJECT
public:
    explicit CasSession(const QString& casUrl, QObject* parent = nullptr);
    ~CasSession();

    // CAS 登录
    void login(const QString& username, const QString& password,
               const std::function<void(bool success)>& callback);

    // 查询登录状态
    bool isLoggedIn() const;

    // 登出
    void logout();

    // 手动续期
    void renew(const std::function<void(bool)>& callback);

    // 会话获取（CAS 认证后的请求）
    void get(const QString& path, const std::function<void(const QByteArray&)>& callback);
    void post(const QString& path, const QByteArray& body,
              const std::function<void(const QByteArray&)>& callback);

signals:
    void loginStatusChanged(bool loggedIn);
    void error(const QString& message);

private:
    class Private;
    Private* d;
};

} // namespace QtAuthNet
