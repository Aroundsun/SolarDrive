#pragma once

#include "../http/http_request.h"
#include "../http/http_response.h"

namespace solar_auth {

// 要求请求已写入 auth 上下文（main 层 JWT 中间件负责）
inline bool require_auth_user(const solar_http::HttpRequest& req,
                              solar_http::HttpResponse& resp) {
    if (req.has_auth_user()) {
        return true;
    }
    resp.set_error(401, "Unauthorized");
    return false;
}

} // namespace solar_auth
