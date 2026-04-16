/**
 * QtAuthNet QML Plugin
 *
 * 将 QtAuthNet::ClientQml、QtAuthNet::CasSessionQml 和
 * QtAuthNet::BrowserCasSession 注册为 QML 模块中的可创建类型。
 *
 * QML 中使用：
 *   import QtAuthNet 1.0
 *
 *   Client { ... }
 *   CasSession { ... }
 *   BrowserCasSession { ... }
 */

#include <QtQml/QQmlExtensionPlugin>
#include <QtQml/qqml.h>

#include <QtAuthNet/ClientQml.h>
#include <QtAuthNet/CasSessionQml.h>
#include <QtAuthNet/BrowserCasSession.h>
#include <QtAuthNet/HttpResponse.h>

class QtAuthNetPlugin : public QQmlExtensionPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QQmlExtensionInterface_iid FILE "qmldir")

public:
    QtAuthNetPlugin(QObject* parent = nullptr) : QQmlExtensionPlugin(parent) {}

    void registerTypes(const char* uri) override {
        Q_ASSERT(QLatin1String(uri) == QLatin1String("QtAuthNet"));

        // HttpResponse 不可在 QML 中构造，仅作为信号参数类型自动注册
        qRegisterMetaType<QtAuthNet::HttpResponse>();

        // ClientQml — HTTP 客户端
        qmlRegisterType<QtAuthNet::ClientQml>(
            uri, 1, 0, "Client");

        // CasSessionQml — CAS 会话（用户名密码方式）
        qmlRegisterType<QtAuthNet::CasSessionQml>(
            uri, 1, 0, "CasSession");

        // BrowserCasSession — 浏览器 CAS 登录（支持 PKI/SSO）
        qmlRegisterType<QtAuthNet::BrowserCasSession>(
            uri, 1, 0, "BrowserCasSession");

        // 版本别名，方便以后升级
        // v1.1: qmlRegisterType<...>(uri, 1, 1, "Client");
    }
};

#include "QtAuthNetPlugin.moc"
