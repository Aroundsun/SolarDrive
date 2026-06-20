#include "auth_handler.h"
#include "../auth/auth_middleware.h"
#include <nlohmann/json.hpp>
#include <ctime>

using json = nlohmann::json;

namespace solar_api {

AuthHandler::AuthHandler(std::shared_ptr<solar_auth::UserDao> user_dao)
    : user_dao_(std::move(user_dao)) {}

void AuthHandler::handle_register(const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
    try {
        json body = json::parse(req.body());
        std::string username = body["username"].get<std::string>();
        std::string password = body["password"].get<std::string>();

        if (username.empty() || password.empty()) {
            resp.set_error(400, "username and password are required");
            return;
        }

        std::string user_id = user_dao_->register_user(username, password);

        // 生成 JWT
        solar_auth::JwtClaims claims;
        claims.user_id  = user_id;
        claims.username = username;
        claims.iat      = std::time(nullptr);
        claims.exp      = 0;  // generate 会用默认 7 天
        std::string token = solar_auth::JwtUtil::generate(claims);

        json j;
        j["user_id"]  = user_id;
        j["username"] = username;
        j["token"]    = token;
        resp.set_json(j.dump());
    } catch (const json::exception& e) {
        resp.set_error(400, std::string("invalid JSON: ") + e.what());
    } catch (const std::exception& e) {
        resp.set_error(500, std::string("register failed: ") + e.what());
    }
}

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

        // 生成 JWT
        solar_auth::JwtClaims claims;
        claims.user_id  = user->id;
        claims.username = user->username;
        claims.iat      = std::time(nullptr);
        claims.exp      = 0;
        std::string token = solar_auth::JwtUtil::generate(claims);

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
