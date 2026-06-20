#pragma once

#include "http_request.h"

#include <string>
#include <vector>
#include <regex>
#include <functional>
#include <memory>

namespace solar_http {

class HttpRequest;
class HttpResponse;

using HttpHandler = std::function<void(const HttpRequest&, HttpResponse&)>;

/// 轻量 HTTP 路由器
/// 支持 GET / POST / PUT / DELETE 方法注册
/// 路径参数用 {name} 声明，如 /files/{id}
class HttpRouter {
public:
    void get(const std::string& pattern, HttpHandler handler);
    void post(const std::string& pattern, HttpHandler handler);
    void put(const std::string& pattern, HttpHandler handler);
    void del(const std::string& pattern, HttpHandler handler);

    /// 分发请求到对应的 handler
    /// 找不到匹配返回 404
    void dispatch(const HttpRequest& req, HttpResponse& resp) const;

private:
    struct Route {
        HttpMethod            method;
        std::regex           regex;
        std::vector<std::string> param_names;
        HttpHandler           handler;
    };

    void add_route(HttpMethod method, const std::string& pattern, HttpHandler handler);
    static std::string pattern_to_regex(const std::string& pattern,
                                        std::vector<std::string>& param_names);

    std::vector<Route> routes_;
};

} // namespace solar_http
