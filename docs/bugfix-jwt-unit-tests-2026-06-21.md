# JWT 缺陷修复记录（单元测试发现）

**日期：** 2026-06-21  
**发现方式：** 新增 `test/auth_test/` 第一阶段单元测试时，`JwtTest.GenerateAndVerifyRoundTrip` 与 `AuthMiddlewareTest.ValidTokenAuthenticatesUser` 持续失败  
**涉及文件：** `src/auth/jwt.cpp`  
**关联测试：** `test/auth_test/test_jwt.cpp`、`test/auth_test/test_auth_middleware.cpp`

---

## 背景

在将 `test/net_test` 接入 CMake 并补充 HTTP / Auth / Storage / WebSocket 单测后，`auth_test` 中 JWT 相关用例无法通过：同一 secret 下 `generate()` 签出的 token，`verify()` 几乎总是返回 `std::nullopt`。

经对比 Python HMAC 与 OpenSSL 手工验签，确认**签名本身正确**，失败发生在 **payload Base64URL 解码** 阶段。进一步审查 `generate()` 还发现一处 **悬垂指针** 隐患。

---

## Bug 1：`header.dump()` / `payload.dump()` 重复调用（悬垂指针）

### 现象

`JwtUtil::generate()` 在编码 Header / Payload 时，对 `dump()` 的返回值调用了两次：

```cpp
// 修复前（错误）
std::string header_b64 = base64url_encode(
    reinterpret_cast<const unsigned char*>(header.dump().data()),
    header.dump().size());
```

`dump()` 每次返回**临时 `std::string`**。第一次 `.data()` 指向临时对象 A，第二次 `.size()` 来自临时对象 B；A 在语句结束前已析构，`.data()` 成为悬垂指针，行为未定义。

Payload 编码存在同样问题。

### 影响

- 在部分编译器 / 优化级别下，可能生成**错误的 Base64 段**或偶发崩溃
- 与 Bug 2 叠加时，会放大「登录后 token 无法校验」的问题

### 修复

先将 JSON 保存到局部变量，再编码：

```cpp
const std::string header_json = header.dump();
const std::string payload_json = payload.dump();

std::string header_b64 = base64url_encode(
    reinterpret_cast<const unsigned char*>(header_json.data()),
    header_json.size());
std::string payload_b64 = base64url_encode(
    reinterpret_cast<const unsigned char*>(payload_json.data()),
    payload_json.size());
```

---

## Bug 2：`base64url_decode()` 未正确处理 padding `=`

### 现象

`verify()` 在比对签名通过后，需解码 payload 的 Base64URL 段。典型 JWT payload（含 `user_id`、`username`、`iat`、`exp`）长度往往不是 4 的倍数，解码前会补 `=`：

```cpp
while (input.size() % 4 != 0)
    input += '=';
```

修复前的解码循环**在检查 `=` 之前**就判定字符非法：

```cpp
// 修复前（错误）
unsigned char c2 = dec[(unsigned char)input[i + 2]];
unsigned char c3 = dec[(unsigned char)input[i + 3]];
if (c0 == 0xFF || c1 == 0xFF || c2 == 0xFF || c3 == 0xFF)
    return std::nullopt;
```

查表 `dec['=']` 为 `0xFF`（未映射），导致**只要 payload 需要 padding，解码必失败**，`verify()` 直接返回 `nullopt`。

### 影响

- **登录后 Bearer Token 校验失败**（`AuthMiddleware::authenticate` 报 `Invalid or expired token`）
- 用户可能表现为：注册/登录接口返回 token，但访问 `/api/v1/files` 等受保护接口一律 401
- 问题与 payload 长度相关，具有隐蔽性（短 payload 可能「碰巧」不需要 padding 而看似正常）

### 修复

仅对非 padding 位置做非法字符检查；遇到 `=` 时跳过对应字节输出：

```cpp
if (input[i + 2] != '=') {
    if (dec[(unsigned char)input[i + 2]] == 0xFF)
        return std::nullopt;
    // ... 解码第三字节
    if (input[i + 3] != '=') {
        if (dec[(unsigned char)input[i + 3]] == 0xFF)
            return std::nullopt;
        // ... 解码第四字节
    }
}
```

---

## 验证方式

### 单元测试

```bash
cmake -S . -B build -DSOLAR_BUILD_TESTS=ON
cmake --build build --target auth_test
./build/test/auth_test
cd build && ctest -R JwtTest --output-on-failure
cd build && ctest -R AuthMiddlewareTest --output-on-failure
```

预期：`JwtTest.*`、`AuthMiddlewareTest.*` 全部通过。

### 手工对比（可选）

对同一 signing input 用 Python 计算 HMAC-SHA256 签名，应与 token 第三段一致；修复后 C++ `verify()` 与 Python 结果一致。

---

## 测试覆盖（修复后）

| 测试文件 | 用例 | 覆盖点 |
|----------|------|--------|
| `test/auth_test/test_jwt.cpp` | `GenerateAndVerifyRoundTrip` | 签发 + 校验闭环 |
| | `RejectsTamperedToken` | 篡改签名 |
| | `RejectsExpiredToken` | 过期 `exp` |
| | `RejectsWrongSecret` | 密钥不一致 |
| | `ExtractTokenFromBearerHeader` | Authorization 解析 |
| `test/auth_test/test_auth_middleware.cpp` | `ValidTokenAuthenticatesUser` | 中间件 + JWT 集成 |
| | `ProtectedPathRequiresAuthorization` | 无 Token 401 |
| | `WhitelistedPathBypassesMissingAuth` | 白名单放行 |

全量 `ctest` 当前：**126 / 126 通过**（含 `net_test`、`http_test`、`auth_test`、`storage_test`、`ws_test`）。

---

## 经验总结

1. **JWT 编解码路径必须有单测闭环**（generate → verify），仅测登录 HTTP 接口不足以覆盖 Base64 padding 边界。
2. **避免对临时对象链式取 `.data()` / `.size()`**，JSON `dump()` 结果应先落盘到命名变量。
3. **Base64 解码需显式处理 padding**，不能假设输入字符均在 alphabet 内。

---

## 相关提交范围

- 修复：`src/auth/jwt.cpp`
- 测试：`test/auth_test/`、`test/CMakeLists.txt`
- 文档：本文档
