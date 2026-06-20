#pragma once

// auth_handler.h — 认证 API 处理器
// 提供用户注册与登录接口，成功后签发 JWT 令牌

#include "../http/http_request.h"
#include "../http/http_response.h"
#include "../auth/user_dao.h"
#include "../auth/jwt.h"
#include <memory>

namespace solar_api {

// 认证处理器：封装注册/登录逻辑，依赖 UserDao 持久化用户信息
class AuthHandler {
public:
    AuthHandler(std::shared_ptr<solar_auth::UserDao> user_dao, int jwt_ttl_hours);

    void handle_register(const solar_http::HttpRequest& req, solar_http::HttpResponse& resp);
    void handle_login(const solar_http::HttpRequest& req, solar_http::HttpResponse& resp);

private:
    std::string issue_token(const std::string& user_id, const std::string& username);

    std::shared_ptr<solar_auth::UserDao> user_dao_;
    int jwt_ttl_hours_;
};

} // namespace solar_api
