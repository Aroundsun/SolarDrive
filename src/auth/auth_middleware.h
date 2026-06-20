// ---------------------------------------------------------------------------
// auth_middleware.h
//
// HTTP 认证中间件：白名单路径豁免与 JWT Bearer Token 校验。
// 将合法请求解析为用户身份（user_id / username），供后续业务处理器使用。
// ---------------------------------------------------------------------------

#pragma once

#include "../http/http_request.h"
#include "../http/http_response.h"
#include "jwt.h"
#include <string>

namespace solar_auth {

// 认证结果：成功时携带用户信息，失败时携带错误描述
struct AuthResult {
    bool        authenticated;  // 是否通过认证
    std::string user_id;        // 用户 UUID（白名单路径为空）
    std::string username;       // 用户名（白名单路径为空）
    std::string error_msg;      // 失败原因（仅 authenticated=false 时有意义）
};

// 认证中间件（纯静态工具类，无实例状态）
class AuthMiddleware {
public:
    // 判断请求路径是否在白名单内（静态资源、公开 API、分享链接等）
    static bool is_whitelisted(const std::string& path);

    // 对 HTTP 请求执行完整认证流程，返回 AuthResult
    static AuthResult authenticate(const solar_http::HttpRequest& req);
};

} // namespace solar_auth
