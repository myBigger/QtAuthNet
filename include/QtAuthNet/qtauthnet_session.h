#pragma once
#include <QtAuthNet/qtauthnet_global.h>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QMap>
#include <QtCore/QVariant>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace QtAuthNet {

class QTAUTHNET_EXPORT CasSession : public QObject {
public:
    using StatusCallback = std::function<void(bool)>;
    using ErrorCallback = std::function<void(const QString&)>;

    void setStatusCallback(StatusCallback cb);
    void setErrorCallback(ErrorCallback cb);
    explicit CasSession(const QString& casUrl, QObject* parent = nullptr);
    ~CasSession();

    void login(const QString& username, const QString& password,
               const std::function<void(bool)>& callback);
    bool isLoggedIn() const;
    void logout();
    void renew(const std::function<void(bool)>& callback);
    void get(const QString& path, const std::function<void(const QByteArray&)>& callback);
    void post(const QString& path, const QByteArray& body,
              const std::function<void(const QByteArray&)>& callback);

private slots:
    void onRenewTimeout();

private:
    void acquireServiceTicket(const QString& service,
                               const std::function<void(const QString&)>& callback);
    void doGetWithST(const QString& path, const QString& st,
                     const std::function<void(const QByteArray&)>& callback);
    void doPostWithST(const QString& path, const QString& st,
                      const QByteArray& body,
                      const std::function<void(const QByteArray&)>& callback);
    QString buildCasUrl(const QString& path) const;

    class Private;
    Private* d;
};

} // namespace QtAuthNet
