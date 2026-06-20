#include "http_response.h"

#include <nlohmann/json.hpp>

namespace solar_http {

void HttpResponse::set_error(int code, const std::string& msg) {
    status_code_ = code;
    status_text_ = msg;
    set_json(nlohmann::json{{"error", msg}}.dump());
}

} // namespace solar_http
