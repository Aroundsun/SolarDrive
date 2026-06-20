// ---------------------------------------------------------------------------
// jwt.h
//
// JWT（JSON Web Token）工具：基于 HMAC-SHA256 的签发与验证。
// 用于用户登录后颁发访问令牌，后续请求通过 Bearer Token 携带身份。
// ---------------------------------------------------------------------------

#pragma once

#include <string>
#include <optional>
#include <chrono>

namespace solar_auth {

// JWT Payload 中的用户声明
struct JwtClaims {
    std::string user_id;   // 用户 UUID
    std::string username;  // 用户名
    int64_t     exp;       // 过期时间戳（秒）
    int64_t     iat;       // 签发时间戳（秒）
};

// JWT 工具类（静态方法，全局共享签名密钥）
class JwtUtil {
public:
    // 设置 HMAC 签名密钥（启动时从配置注入）
    static void set_secret(const std::string& secret);

    // 签发 JWT，默认有效期 ttl_hours 小时（168 = 7 天）
    static std::string generate(const JwtClaims& claims, int ttl_hours = 168);

    // 验证 JWT 签名与过期时间，成功返回解析后的声明
    static std::optional<JwtClaims> verify(const std::string& token);

    // 从 "Bearer <token>" 格式的 Authorization 头中提取 token 字符串
    static std::optional<std::string> extract_token(const std::string& auth_header);

private:
    static std::string secret_;
    static std::string base64url_encode(const unsigned char* data, size_t len);
    static std::optional<std::string> base64url_decode(const std::string& str);
    static std::string hmac_sha256(const std::string& key, const std::string& msg);
};

} // namespace solar_auth
