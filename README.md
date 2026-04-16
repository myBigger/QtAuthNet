# QtAuthNet

**一个让 Qt HTTP 请求从繁琐变简洁的轻量封装库。**

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![Qt Version](https://img.shields.io/badge/Qt-5.15%20|%206.0+-green.svg)](https://doc.qt.io/)

---

## 解决的问题

你一定遇到过这两种情况：

**第一种：每次写 HTTP 请求，都在抄自己的代码。**

```cpp
// 这六行代码，你写了多少次？
QNetworkAccessManager* manager = new QNetworkAccessManager(this);
QNetworkRequest request;
request.setUrl(QUrl("https://api.example.com/users"));
request.setRawHeader("Authorization", "Bearer " + token);

connect(manager, &QNetworkAccessManager::finished, this, [&](QNetworkReply* reply) {
    // 处理响应...
});
manager->get(request);
```

每次只有 URL、Header、POST 数据不一样，剩下六行一模一样。

**第二种：会话做到一半，token 过期了。**

CAS 单点登录的网站，token 有时效，cookie 有生命周期。一旦过期，要么弹窗让用户重新登录，要么程序直接报错——因为你根本没处理"过期"这个分支。

QtAuthNet 就是来解决这两个问题的。

## 核心特性

- **零样板代码**：告别每次都要写的 `new QNetworkAccessManager` + `connect` + `setUrl` + `setHeader`
- **自动会话管理**：token 过期自动刷新，cookie 自动维护，无需手动处理
- **CAS 单点登录支持**：内置 Ticket/Token 生命周期管理，登录状态自动保持
- **链式 API**：请求配置一目了然，从发起到响应，全流程可读
- **Qt5 / Qt6 双支持**：一个库，两代 Qt 版本都能用

## 快速开始

### 安装

```bash
# 方式一：直接拷贝源码（推荐，快速上手）
# 将 src/ 目录下的文件加入你的 Qt 项目即可

# 方式二：作为子模块
git submodule add https://github.com/myBigger/QtAuthNet.git
```

### 三行代码，发一个请求

```cpp
// 最简单的 GET 请求
QtAuthNet::Client client("https://api.example.com");
client.get("/users/me", [](const QByteArray& data) {
    qDebug() << "响应:" << data;
});
```

```cpp
// 带 Bearer Token 的 POST 请求
QtAuthNet::Client client("https://api.example.com");
client.setBearerToken("your-access-token");

QJsonObject body;
body["username"] = "bigege";
body["password"] = "secret";

client.post("/login", body, [](const QJsonDocument& resp) {
    qDebug() << resp;
});
```

### 自动续期：不用再管 token 过期

```cpp
// 设置刷新令牌的回调，QtAuthNet 会自动处理过期逻辑
client.setTokenRefreshCallback([&]() {
    // 重新获取 access token 的逻辑写在这里
    return refreshAccessToken();
});

// 之后的请求，QtAuthNet 自动判断 token 状态
// 有效 → 直接用
// 过期 → 自动调用回调刷新 → 重试原请求
// 对调用方完全透明
client.get("/profile", [](const QJsonDocument& resp) {
    qDebug() << resp;
});
```

### CAS 单点登录：会话管理，交给我们

```cpp
QtAuthNet::CasSession session("https://cas.example.com");

// 登录一次，后续请求自动携带 Cookie 和 Ticket
session.login("username", "password", [&]() {
    // 登录成功后，发起需要认证的请求
    session.get("/protected-resource", [](const QByteArray& data) {
        qDebug() << data;
    });
});
```

## API 概览

### QtAuthNet::Client — HTTP 请求客户端

| 方法 | 说明 |
|------|------|
| `Client(const QString& baseUrl)` | 构造函数，传入 base URL |
| `setBearerToken(token)` | 设置 Bearer Token |
| `setBasicAuth(user, pass)` | 设置 Basic Auth |
| `setApiKey(key, location)` | 设置 API Key（Header / Query）|
| `setHeader(key, value)` | 设置自定义 Header |
| `setTokenRefreshCallback(fn)` | 设置 Token 自动刷新回调 |
| `get(path, callback)` | GET 请求 |
| `post(path, body, callback)` | POST JSON 请求 |
| `postForm(path, data, callback)` | POST Form 请求 |
| `put(path, body, callback)` | PUT 请求 |
| `delete(path, callback)` | DELETE 请求 |

### QtAuthNet::CasSession — CAS 会话管理

| 方法 | 说明 |
|------|------|
| `CasSession(const QString& casUrl)` | 构造函数，传入 CAS 服务器地址 |
| `login(user, pass, callback)` | CAS 单点登录 |
| `isLoggedIn()` | 查询登录状态 |
| `logout()` | 登出，清除会话 |
| `renew()` | 手动续期会话 |

## 对比

| | 原生 QNetworkAccessManager | QtAuthNet |
|---|---|---|
| 写一个 GET 请求 | 6 行样板代码 | 1 行 |
| Bearer Token | 手动设置 Header | `setBearerToken()` |
| Token 过期 | 程序报错或静默失败 | 自动刷新 + 重试 |
| CAS 会话管理 | 手动维护 Cookie | 内置生命周期管理 |
| 请求取消 | 需自行管理 reply 对象 | `client.cancel()` |

## 适用场景

- Qt 桌面 / 嵌入式应用中需要调用 HTTP API 的开发者
- 使用 CAS 单点登录的企业应用
- 需要简化 Qt HTTP 请求代码量的任何场景

## 暂不支持

- WebSocket（未来版本规划中）
- 文件上传 / 下载进度跟踪（v2.0 规划中）

---

## QML 接口（v1.1 新增）

从 v1.1 开始，QtAuthNet 提供原生 QML 接口，无需写 C++ 即可在 QML 中使用。

### 安装

```bash
# 启用 QML 插件（需要 Qt5 Qml 或 Qt6 Qml 模块）
cmake -B build -DQTAUTHNET_BUILD_QML_PLUGIN=ON
cmake --build build
sudo cmake --install build
```

### 快速开始

```qml
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtAuthNet 1.0

Client {
    id: api
    baseUrl: "https://api.example.com"
    bearerToken: "your-token"

    // 监听响应
    onResponseReady: function(reqId, resp) {
        if (resp.ok) {
            console.log("数据:", resp.json)
            resultText.text = JSON.stringify(resp.json, null, 2)
        } else {
            errorText.text = resp.errorMessage()
        }
    }
    onError: function(msg) {
        console.error("网络错误:", msg)
    }

    // 发起请求
    Button {
        text: "获取用户信息"
        onClicked: api.get("/users/me")
    }
}
```

### ClientQml API

| 属性 / 方法 | 类型 | 说明 |
|------------|------|------|
| `baseUrl` | string | API 基础地址 |
| `bearerToken` | string | Bearer Token |
| `basicUser` / `basicPass` | string | Basic Auth |
| `apiKey` | string | API Key（配合 location）|
| `timeout` | int | 请求超时（毫秒）|
| `loading` | bool | 是否有请求进行中 |
| `get(path)` | void | GET 请求 |
| `postJson(path, json)` | void | POST JSON 请求 |
| `post(path, body, contentType)` | void | POST 原始数据 |
| `put(path, body, contentType)` | void | PUT 请求 |
| `del(path)` | void | DELETE 请求 |
| `cancel()` | void | 取消所有请求 |

**信号：**

| 信号 | 说明 |
|------|------|
| `responseReady(reqId, resp)` | 请求完成，`resp.statusCode` / `resp.ok` / `resp.json` |
| `error(message)` | 网络或业务错误 |
| `loadingChanged(loading)` | 加载状态变更 |
| `tokenRefreshRequested()` | 收到 401，QML 侧应获取新 token 后调 `refreshToken(token)` |

### CasSessionQml API

| 属性 / 方法 | 类型 | 说明 |
|------------|------|------|
| `baseUrl` | string | CAS 服务器地址 |
| `loggedIn` | bool | 登录状态 |
| `username` | string | 当前用户名 |
| `login(user, pass)` | void | CAS 登录 |
| `logout()` | void | CAS 登出 |
| `get(path)` | void | 发起需认证的 GET |
| `postJson(path, json)` | void | 发起需认证的 POST JSON |

**信号：**

| 信号 | 说明 |
|------|------|
| `loginStatusChanged(loggedIn)` | 登录状态变更 |
| `responseReady(reqId, resp)` | 业务请求响应 |
| `error(message)` | 错误信息 |

## 许可证

[MIT License](LICENSE) — 可自由用于商业和非商业项目。
