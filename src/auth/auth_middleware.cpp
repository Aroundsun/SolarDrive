// ---------------------------------------------------------------------------
// auth_middleware.cpp
//
// 认证中间件实现：维护白名单规则，从 Authorization 头提取并验证 JWT。
// ---------------------------------------------------------------------------

#include "auth_middleware.h"
#include "jwt.h"
#include <cstring>
#include <vector>
#include <string>

namespace solar_auth {

bool AuthMiddleware::is_whitelisted(const std::string& path) {
    // 前端静态资源，无需登录即可访问
    if (path == "/" || path == "/index.html" || path == "/app.js" ||
        path == "/style.css" || path == "/favicon.ico" ||
        path == "/login.html" || path == "/auth.js" || path == "/session.js" ||
        path == "/receive.html" || path == "/receive.js" ||
        path == "/icons.js" ||
        path == "/metrics.html" || path == "/metrics.js" ||
        path == "/share.html" || path == "/share.js") {
        return true;
    }

    // 公开 API 前缀（健康检查、注册/登录、Prometheus 指标）
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

    // 公开分享下载：/s/{token}，无需 Bearer Token
    if (path.size() > 3 && path.compare(0, 3, "/s/") == 0) {
        return true;
    }

    return false;
}

AuthResult AuthMiddleware::authenticate(const solar_http::HttpRequest& req) {
    AuthResult result;
    result.authenticated = false;

    // 白名单路径直接放行（调用方通常已预检，此处二次兜底）
    if (is_whitelisted(req.path())) {
        result.authenticated = true;
        return result;
    }

    // 非白名单路径必须携带 Authorization 头
    std::string auth_header = req.get_header("Authorization");
    if (auth_header.empty()) {
        result.error_msg = "Missing Authorization header";
        return result;
    }

    // 解析 "Bearer <token>" 格式
    auto token = JwtUtil::extract_token(auth_header);
    if (!token) {
        result.error_msg = "Invalid Authorization format, expected: Bearer <token>";
        return result;
    }

    // 校验 JWT 签名与过期时间，提取用户声明
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
