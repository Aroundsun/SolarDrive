#pragma once

#include "../http/http_request.h"
#include "../http/http_response.h"
#include "jwt.h"
#include <string>

namespace solar_auth {

struct AuthResult {
    bool        authenticated;
    std::string user_id;
    std::string username;
    std::string error_msg;
};

class AuthMiddleware {
public:
    static bool is_whitelisted(const std::string& path);
    static AuthResult authenticate(const solar_http::HttpRequest& req);
};

} // namespace solar_auth
