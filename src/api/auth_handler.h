#pragma once

#include "../http/http_request.h"
#include "../http/http_response.h"
#include "../auth/user_dao.h"
#include "../auth/jwt.h"
#include <memory>

namespace solar_api {

class AuthHandler {
public:
    explicit AuthHandler(std::shared_ptr<solar_auth::UserDao> user_dao);

    void handle_register(const solar_http::HttpRequest& req, solar_http::HttpResponse& resp);
    void handle_login(const solar_http::HttpRequest& req, solar_http::HttpResponse& resp);

private:
    std::shared_ptr<solar_auth::UserDao> user_dao_;
};

} // namespace solar_api
