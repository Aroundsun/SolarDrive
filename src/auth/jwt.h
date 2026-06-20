#pragma once

#include <string>
#include <optional>
#include <chrono>

namespace solar_auth {

struct JwtClaims {
    std::string user_id;
    std::string username;
    int64_t     exp;   // 过期时间戳（秒）
    int64_t     iat;   // 签发时间戳（秒）
};

class JwtUtil {
public:
    static void set_secret(const std::string& secret);
    static std::string generate(const JwtClaims& claims, int ttl_hours = 168);
    static std::optional<JwtClaims> verify(const std::string& token);
    static std::optional<std::string> extract_token(const std::string& auth_header);

private:
    static std::string secret_;
    static std::string base64url_encode(const unsigned char* data, size_t len);
    static std::optional<std::string> base64url_decode(const std::string& str);
    static std::string hmac_sha256(const std::string& key, const std::string& msg);
};

} // namespace solar_auth
