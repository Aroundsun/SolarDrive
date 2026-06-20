#include "http_request.h"

namespace solar_http {

namespace {

std::string to_lower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool header_contains_token(const std::string& value, const std::string& token) {
    const std::string lower = to_lower_copy(value);
    const std::string needle = to_lower_copy(token);
    return lower.find(needle) != std::string::npos;
}

} // namespace

bool HttpRequest::wants_keep_alive() const {
    const std::string connection = get_header_ic("Connection");
    if (header_contains_token(connection, "close")) {
        return false;
    }
    if (header_contains_token(connection, "keep-alive")) {
        return true;
    }

    const std::string version = to_lower_copy(version_);
    if (version.find("1.0") != std::string::npos) {
        return false;
    }
    return true;
}

} // namespace solar_http
