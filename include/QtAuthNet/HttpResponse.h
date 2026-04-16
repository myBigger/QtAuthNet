#pragma once
#include <QtCore/QByteArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QVariant>
#include <QtCore/QMetaType>

namespace QtAuthNet {

/**
 * HttpResponse — QML 可用的 HTTP 响应封装
 *
 * 用 Q_GADGET 而非 Q_OBJECT，避免信号参数类型注册麻烦。
 * QML 直接通过 QVariant 读取所有字段。
 *
 * QML 用法：
 *   onResponseReady: (resp) => {
 *       console.log(resp.statusCode, resp.json)
 *   }
 */
class HttpResponse {
    Q_GADGET

public:
    HttpResponse() = default;
    HttpResponse(int code, const QByteArray& raw, bool ok)
        : m_statusCode(code), m_rawData(raw), m_ok(ok)
    {
        if (ok && !raw.isEmpty()) {
            m_jsonDoc = QJsonDocument::fromJson(raw);
            if (m_jsonDoc.isObject()) m_json = m_jsonDoc.object();
            else if (m_jsonDoc.isArray()) m_json = QJsonObject{{"array", m_jsonDoc.array()}};
        }
    }

    // ── 状态 ──────────────────────────────────────
    Q_PROPERTY(int statusCode READ statusCode CONSTANT)
    int statusCode() const { return m_statusCode; }

    Q_PROPERTY(bool ok READ ok CONSTANT)
    bool ok() const { return m_ok; }

    // ── 原始数据 ──────────────────────────────────
    Q_PROPERTY(QByteArray rawData READ rawData CONSTANT)
    QByteArray rawData() const { return m_rawData; }

    // ── JSON（自动解析）───────────────────────────
    // QML 中直接用 response.json["key"]
    Q_PROPERTY(QJsonObject json READ json CONSTANT)
    QJsonObject json() const { return m_json; }

    // ── 便捷辅助 ─────────────────────────────────
    // QML 用法: response.jsonOrDefault({"name": ""})["name"].toString()
    Q_INVOKABLE QVariant jsonOrDefault(const QJsonObject& defaults) const {
        QJsonObject merged = defaults;
        for (auto it = m_json.constBegin(); it != m_json.constEnd(); ++it)
            merged[it.key()] = it.value();
        return merged;
    }

    Q_INVOKABLE QString errorMessage() const {
        if (m_ok) return QString();
        return QString::fromUtf8(m_rawData).left(200);
    }

private:
    int m_statusCode = 0;
    QByteArray m_rawData;
    QJsonObject m_json;
    QJsonDocument m_jsonDoc;
    bool m_ok = false;
};

} // namespace QtAuthNet

// 让 QVariant / QML 能识别 HttpResponse 类型
Q_DECLARE_METATYPE(QtAuthNet::HttpResponse)
