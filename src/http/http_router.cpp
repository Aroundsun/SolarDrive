#include "http_router.h"
#include "http_response.h"

#include <cstring>

namespace solar_http {

void HttpRouter::get(const std::string& pattern, HttpHandler handler) {
    add_route(HttpMethod::GET, pattern, std::move(handler));
}

void HttpRouter::post(const std::string& pattern, HttpHandler handler) {
    add_route(HttpMethod::POST, pattern, std::move(handler));
}

void HttpRouter::put(const std::string& pattern, HttpHandler handler) {
    add_route(HttpMethod::PUT, pattern, std::move(handler));
}

void HttpRouter::del(const std::string& pattern, HttpHandler handler) {
    add_route(HttpMethod::DELETE, pattern, std::move(handler));
}

void HttpRouter::add_route(HttpMethod method,
                           const std::string& pattern,
                           HttpHandler handler) {
    Route r;
    r.method     = method;
    r.handler    = std::move(handler);
    r.regex      = std::regex(pattern_to_regex(pattern, r.param_names));
    routes_.push_back(std::move(r));
}

std::string HttpRouter::pattern_to_regex(const std::string& pattern,
                                            std::vector<std::string>& param_names) {
    std::string result;
    result.reserve(pattern.size() * 2);
    param_names.clear();

    // 先对正则特殊字符进行转义
    // 但保留 { } 不转义，后面单独处理
    for (size_t i = 0; i < pattern.size(); ++i) {
        char c = pattern[i];
        if (c == '{') {
            // 找到匹配的 }
            size_t end = pattern.find('}', i);
            if (end == std::string::npos) {
                // 格式错误，原样输出
                result += c;
                continue;
            }
            std::string param_name = pattern.substr(i + 1, end - i - 1);
            param_names.push_back(param_name);
            result += "([^/]+)";  // 捕获组：非 / 的任意字符
            i = end;
        } else {
            // 转义正则特殊字符
            if (strchr(".+*?^$|()\\[]{}", c)) {
                result += '\\';
            }
            result += c;
        }
    }
    return result;
}

void HttpRouter::dispatch(const HttpRequest& req, HttpResponse& resp) const {
    for (const auto& r : routes_) {
        if (r.method != req.method()) continue;

        std::regex_constants::match_flag_type flags =
            std::regex_constants::match_default;
        std::cmatch match;
        if (!std::regex_match(req.path().c_str(), match, r.regex, flags)) {
            continue;
        }

        // 填充 path_params
        auto& mutable_req = const_cast<HttpRequest&>(req);
        mutable_req.clear_path_params();
        for (size_t i = 0; i < r.param_names.size() && i + 1 < match.size(); ++i) {
            mutable_req.set_path_param(r.param_names[i], match[i + 1].str());
        }

        r.handler(req, resp);
        return;
    }

    resp.set_error(404, "Not Found");
}

} // namespace solar_http
