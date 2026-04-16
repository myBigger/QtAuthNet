#include <QtTest/QtTest>
#include <QtAuthNet/HttpResponse.h>
#include <QtCore/QJsonDocument>

class TestHttpResponse : public QObject {
    Q_OBJECT

private slots:
    void init() {}
    void cleanup() {}

    void testDefaultConstructor() {
        QtAuthNet::HttpResponse resp;
        QVERIFY(resp.statusCode() == 0);
        QVERIFY(!resp.ok());
        QVERIFY(resp.rawData().isEmpty());
        QVERIFY(resp.json().isEmpty());
    }

    void testSuccessConstructor() {
        QByteArray jsonData = R"({"code":200,"message":"ok"})";
        QtAuthNet::HttpResponse resp(200, jsonData, true);
        QVERIFY(resp.statusCode() == 200);
        QVERIFY(resp.ok());
        QVERIFY(resp.rawData() == jsonData);
        QVERIFY(!resp.json().isEmpty());
        QVERIFY(resp.json().value(QStringLiteral("code")).toInt() == 200);
    }

    void testErrorConstructor() {
        QtAuthNet::HttpResponse resp(404, QByteArray("Not Found"), false);
        QVERIFY(resp.statusCode() == 404);
        QVERIFY(!resp.ok());
    }

    void testJsonOrDefault() {
        QByteArray jsonData = R"({"name":"test"})";
        QtAuthNet::HttpResponse resp(200, jsonData, true);

        QJsonObject defaults;
        defaults.insert(QStringLiteral("name"), QStringLiteral("default"));
        defaults.insert(QStringLiteral("age"), 0);

        QJsonObject result = resp.jsonOrDefault(defaults).toJsonObject();
        QVERIFY(result.value(QStringLiteral("name")).toString() == QStringLiteral("test"));
        QVERIFY(result.value(QStringLiteral("age")).toInt() == 0);
    }

    void testJsonOrDefaultWithMissingKey() {
        QByteArray jsonData = R"({})";
        QtAuthNet::HttpResponse resp(200, jsonData, true);

        QJsonObject defaults;
        defaults.insert(QStringLiteral("count"), 42);

        QJsonObject result = resp.jsonOrDefault(defaults).toJsonObject();
        QVERIFY(result.value(QStringLiteral("count")).toInt() == 42);
    }

    void testErrorMessage() {
        QtAuthNet::HttpResponse resp(500, QByteArray("Internal Error"), false);
        // errorMessage() returns the raw data for failed responses
        QVERIFY(resp.errorMessage() == QStringLiteral("Internal Error"));
    }

    void testErrorMessageOk() {
        QtAuthNet::HttpResponse resp(200, QByteArray("OK"), true);
        QVERIFY(resp.errorMessage().isEmpty());
    }
};

QTEST_MAIN(TestHttpResponse)
#include "tst_http_autogen/include/tst_http.moc"
