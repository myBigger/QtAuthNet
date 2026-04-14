#pragma once
#include "qtauthnet_global.h"
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QMap>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

namespace QtAuthNet {

class QTAUTHNET_EXPORT Client : public QObject {
    Q_OBJECT
public:
    explicit Client(const QString& baseUrl, QObject* parent = nullptr);
    ~Client();

    void setBearerToken(const QString& token);
    void setBasicAuth(const QString& username, const QString& password);
    void setApiKey(const QString& key, const QString& location = QStringLiteral("header"));
    void setHeader(const QString& key, const QString& value);
    using RefreshCallback = std::function<QString()>;
    void setTokenRefreshCallback(RefreshCallback callback);

    void get(const QString& path, const std::function<void(const QByteArray&)>& callback);
    void post(const QString& path, const QByteArray& body,
              const std::function<void(const QByteArray&)>& callback);
    void postJson(const QString& path, const QVariant& json,
                  const std::function<void(const QByteArray&)>& callback);
    void put(const QString& path, const QByteArray& body,
             const std::function<void(const QByteArray&)>& callback);
    void deleteResource(const QString& path,
                        const std::function<void(const QByteArray&)>& callback);
    void cancel();

signals:
    void error(const QString& message);

private slots:
    void onReplyFinished();

private:
    void executeRequest(const QString& method, const QString& path,
                        const QByteArray& body,
                        const std::function<void(const QByteArray&)>& callback,
                        const QString& contentType = QString());

    QUrl resolvePath(const QString& path) const;

    class Private;
    Private* d;
};

} // namespace QtAuthNet
