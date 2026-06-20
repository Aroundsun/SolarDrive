#include "auth_middleware.h"
#include "jwt.h"
#include <cstring>
#include <vector>
#include <string>

namespace solar_auth {

bool AuthMiddleware::is_whitelisted(const std::string& path) {
    if (path == "/" || path == "/index.html" || path == "/app.js" ||
        path == "/style.css" || path == "/favicon.ico" ||
        path == "/metrics.html" || path == "/metrics.js") {
        return true;
    }

    static const std::vector<std::string> api_whitelist = {
        "/api/v1/health",
        "/api/v1/auth/register",
        "/api/v1/auth/login",
        "/metrics",
    };
    for (const auto& prefix : api_whitelist) {
        if (path.compare(0, prefix.size(), prefix) == 0) {
            return true;
        }
    }
    return false;
}

AuthResult AuthMiddleware::authenticate(const solar_http::HttpRequest& req) {
    AuthResult result;
    result.authenticated = false;

    // 先走白名单检查（调用方已在外面检查过，这里再查一次兜底）
    if (is_whitelisted(req.path())) {
        result.authenticated = true;
        return result;
    }

    // 提取 Authorization header
    std::string auth_header = req.get_header("Authorization");
    if (auth_header.empty()) {
        result.error_msg = "Missing Authorization header";
        return result;
    }

    // 提取 token
    auto token = JwtUtil::extract_token(auth_header);
    if (!token) {
        result.error_msg = "Invalid Authorization format, expected: Bearer <token>";
        return result;
    }

    // 验证 JWT
    auto claims = JwtUtil::verify(*token);
    if (!claims) {
        result.error_msg = "Invalid or expired token";
        return result;
    }

    result.authenticated = true;
    result.user_id  = claims->user_id;
    result.username = claims->username;
    return result;
}

} // namespace solar_auth
