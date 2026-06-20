#pragma once

#include "http_request.h"
#include <string>

namespace solar_http {

// 从 query string 提取单个参数（不做 URL decode，与现有行为一致）
inline std::string query_param(const std::string& query, const std::string& key) {
    const std::string prefix = key + "=";
    const auto pos = query.find(prefix);
    if (pos == std::string::npos) {
        return "";
    }
    const auto start = pos + prefix.size();
    const auto end = query.find('&', start);
    return query.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

inline std::string get_query_param(const HttpRequest& req, const std::string& key) {
    return query_param(req.query(), key);
}

} // namespace solar_http
