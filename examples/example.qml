/**
 * QtAuthNet QML 使用示例
 *
 * 运行方式（假设 QtAuthNet 已安装到系统）：
 *   qmlscene examples/example.qml
 *
 * 或在 Qt Creator 中直接运行。
 *
 * 如果 QML 模块未安装到系统，可用 qmlscene -I <qml_install_dir> 加载。
 */

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtAuthNet 1.0

Rectangle {
    id: root
    width: 480
    height: 640
    color: "#f5f5f5"

    // ══════════════════════════════════════════════════
    //  1. 普通 API 调用示例 — ClientQml
    // ══════════════════════════════════════════════════
    ClientQml {
        id: apiClient

        // API 基础地址
        baseUrl: "https://api.example.com"

        // 认证方式（三选一，均可随时切换）
        // bearerToken: tokenField.text
        // basicUser: "admin"
        // basicPass: "secret"
        // apiKey: "your-api-key"
        // apiKeyLocation: "header"   // 或 "query"

        // ── 监听响应信号 ──────────────────────────────
        onResponseReady: function(reqId, resp) {
            console.log(">>> [请求#" + reqId + "] HTTP", resp.statusCode)

            if (!resp.ok) {
                errorBanner.text = "请求失败: " + resp.errorMessage()
                errorBanner.visible = true
                return
            }

            // 响应数据在 resp.json（QJsonObject）
            var data = resp.json
            console.log(">>> 数据:", JSON.stringify(data))
            resultArea.text = JSON.stringify(data, null, 2)
            errorBanner.visible = false
        }

        onError: function(msg) {
            console.error("!!! 网络错误:", msg)
            errorBanner.text = msg
            errorBanner.visible = true
        }

        // ── 收到 401 时的 Token 刷新回调 ─────────────
        // QML 侧监听此信号，从服务端获取新 token，再调 refreshToken()
        // onTokenRefreshRequested: refreshTokenFromServer()
    }

    // ══════════════════════════════════════════════════
    //  2. CAS 单点登录示例 — CasSessionQml
    // ══════════════════════════════════════════════════
    CasSessionQml {
        id: casSession

        // CAS 服务器地址
        baseUrl: "https://cas.example.com"

        // 会话续期间隔（秒），默认 3600
        // renewIntervalSec: 7200

        onLoginStatusChanged: function(loggedIn) {
            loginStatusLabel.text = loggedIn ? "已登录: " + casSession.username : "未登录"
            loginBtn.enabled = !loggedIn
            logoutBtn.enabled = loggedIn
        }

        onError: function(msg) {
            console.error("!!! CAS 错误:", msg)
            errorBanner.text = msg
            errorBanner.visible = true
        }

        onResponseReady: function(reqId, resp) {
            if (!resp.ok) return
            console.log(">>> [CAS 请求#" + reqId + "]:", resp.rawData)
        }
    }

    // ══════════════════════════════════════════════════
    //  3. UI
    // ══════════════════════════════════════════════════
    Column {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        // 标题
        Label {
            text: "QtAuthNet QML 示例"
            font.pixelSize: 22
            font.bold: true
            color: "#333"
        }

        // 错误提示
        Rectangle {
            id: errorBanner
            width: parent.width
            height: errorBanner.text ? implicitHeight + 16 : 0
            color: "#ffebee"
            radius: 6
            visible: false
            clip: true

            Label {
                anchors.fill: parent
                anchors.margins: 8
                text: errorBanner.text
                color: "#c62828"
                wrapMode: Text.WordWrap
            }
        }

        // ── ClientQml 测试区 ──────────────────────────
        Label { text: "【ClientQml】普通 API 调用"; font.bold: true; color: "#1565c0" }

        TextField {
            id: pathField
            width: parent.width
            placeholderText: "请求路径，如 /users/me"
            text: "/users/me"
        }

        Row {
            spacing: 8
            Button {
                text: "GET"
                onClicked: apiClient.get(pathField.text)
            }
            Button {
                text: "POST JSON"
                onClicked: apiClient.postJson(pathField.text,
                    {"name": "bigege", "action": "from QML"})
            }
            Button {
                text: "DELETE"
                onClicked: apiClient.del(pathField.text)
            }
        }

        Rectangle { width: parent.width; height: 1; color: "#ddd" }

        // ── CasSession 测试区 ──────────────────────────
        Label { text: "【CasSessionQml】CAS 单点登录"; font.bold: true; color: "#2e7d32" }

        Label { id: loginStatusLabel; text: "未登录"; color: "#666" }

        Row {
            spacing: 8
            TextField {
                id: usernameField
                placeholderText: "用户名"
                width: 140
            }
            TextField {
                id: passwordField
                placeholderText: "密码"
                echoMode: TextInput.Password
                width: 140
            }
        }

        Row {
            spacing: 8
            Button {
                id: loginBtn
                text: "登录 CAS"
                onClicked: casSession.login(usernameField.text, passwordField.text)
            }
            Button {
                id: logoutBtn
                text: "登出"
                enabled: false
                onClicked: casSession.logout()
            }
            Button {
                text: "获取受保护资源"
                enabled: casSession.loggedIn
                onClicked: casSession.get("/api/protected")
            }
        }

        Rectangle { width: parent.width; height: 1; color: "#ddd" }

        // ── 结果展示 ─────────────────────────────────
        Label { text: "响应结果"; font.bold: true; color: "#555" }

        ScrollView {
            width: parent.width
            height: 240
            clip: true
            TextArea {
                id: resultArea
                readOnly: true
                wrapMode: Text.WordWrap
                font.family: "monospace"
                font.pixelSize: 11
                text: "// 响应将显示在这里"
                placeholderText: "// 响应将显示在这里"
            }
        }

        // ── 状态栏 ──────────────────────────────────
        Label {
            text: "ClientQml 请求中: " + apiClient.loading
            color: "#999"
            font.pixelSize: 11
        }
    }
}
