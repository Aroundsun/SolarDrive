// =============================================================================
// http_router.h — HTTP 路由注册与分发
// SolarDrive HTTP 层：支持路径参数 {name} 的轻量路由器
// =============================================================================
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

/// 业务处理函数类型：接收请求与响应引用，由 handler 填充 resp
using HttpHandler = std::function<void(const HttpRequest&, HttpResponse&)>;

/// 轻量 HTTP 路由器
/// 支持 GET / POST / PUT / DELETE 方法注册
/// 路径参数用 {name} 声明，如 /files/{id}
class HttpRouter {
public:
    /// 注册 GET 路由
    void get(const std::string& pattern, HttpHandler handler);
    /// 注册 POST 路由
    void post(const std::string& pattern, HttpHandler handler);
    /// 注册 PUT 路由
    void put(const std::string& pattern, HttpHandler handler);
    /// 注册 DELETE 路由
    void del(const std::string& pattern, HttpHandler handler);

    /// 分发请求到对应的 handler
    /// 找不到匹配返回 404
    void dispatch(const HttpRequest& req, HttpResponse& resp) const;

private:
    /// 单条路由：方法 + 正则 + 参数名列表 + 处理函数
    struct Route {
        HttpMethod            method;
        std::regex           regex;
        std::vector<std::string> param_names;
        HttpHandler           handler;
    };

    /// 内部统一注册入口，将 pattern 编译为正则后存入 routes_
    void add_route(HttpMethod method, const std::string& pattern, HttpHandler handler);
    /// 将 /files/{id} 转为正则，并收集 {id} 等参数名
    static std::string pattern_to_regex(const std::string& pattern,
                                        std::vector<std::string>& param_names);

    std::vector<Route> routes_;
};

} // namespace solar_http
