// auth_handler.cpp — 认证 API 实现
// 解析 JSON 请求体，调用 UserDao 完成注册/登录，统一签发 JWT

#include "auth_handler.h"
#include <nlohmann/json.hpp>
#include <ctime>

using json = nlohmann::json;

namespace solar_api {

AuthHandler::AuthHandler(std::shared_ptr<solar_auth::UserDao> user_dao, int jwt_ttl_hours)
    : user_dao_(std::move(user_dao))
    , jwt_ttl_hours_(jwt_ttl_hours > 0 ? jwt_ttl_hours : 168) {}

std::string AuthHandler::issue_token(const std::string& user_id, const std::string& username) {
    solar_auth::JwtClaims claims;
    claims.user_id  = user_id;
    claims.username = username;
    claims.iat      = std::time(nullptr);
    claims.exp      = 0;
    return solar_auth::JwtUtil::generate(claims, jwt_ttl_hours_);
}

void AuthHandler::handle_register(const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
    try {
        json body = json::parse(req.body());
        std::string username = body["username"].get<std::string>();
        std::string password = body["password"].get<std::string>();

        if (username.empty() || password.empty()) {
            resp.set_error(400, "username and password are required");
            return;
        }

        auto user_id = user_dao_->register_user(username, password);
        if (!user_id) {
            resp.set_error(409, "username already exists");
            return;
        }

        // 生成 JWT
        const std::string token = issue_token(*user_id, username);

        json j;
        j["user_id"]  = *user_id;
        j["username"] = username;
        j["token"]    = token;
        resp.set_json(j.dump());
    } catch (const json::exception& e) {
        resp.set_error(400, std::string("invalid JSON: ") + e.what());
    } catch (const std::exception& e) {
        resp.set_error(500, std::string("register failed: ") + e.what());
    }
}

// 登录：校验用户名密码，失败返回 401
void AuthHandler::handle_login(const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
    try {
        json body = json::parse(req.body());
        std::string username = body["username"].get<std::string>();
        std::string password = body["password"].get<std::string>();

        if (username.empty() || password.empty()) {
            resp.set_error(400, "username and password are required");
            return;
        }

        auto user = user_dao_->login(username, password);
        if (!user) {
            resp.set_error(401, "invalid username or password");
            return;
        }

        const std::string token = issue_token(user->id, user->username);

        json j;
        j["user_id"]  = user->id;
        j["username"] = user->username;
        j["token"]    = token;
        resp.set_json(j.dump());
    } catch (const json::exception& e) {
        resp.set_error(400, std::string("invalid JSON: ") + e.what());
    } catch (const std::exception& e) {
        resp.set_error(500, std::string("login failed: ") + e.what());
    }
}

} // namespace solar_api
