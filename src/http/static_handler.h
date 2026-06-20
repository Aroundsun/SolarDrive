// =============================================================================
// static_handler.h — 静态资源服务
// SolarDrive HTTP 层：从 web/ 目录提供前端页面与静态文件
// =============================================================================
#pragma once

#include "http_request.h"
#include "http_response.h"

#include <string>

namespace solar_http {

/// 从本地目录提供静态文件（web/）
class StaticHandler {
public:
    explicit StaticHandler(std::string root_dir);

    /// GET 请求且路径合法时写入 resp 并返回 true
    bool try_serve(const HttpRequest& req, HttpResponse& resp) const;

private:
    /// URL 路径映射为相对文件路径（/ → index.html）
    static std::string map_url_to_file(const std::string& path);
    /// 根据扩展名推断 Content-Type
    static std::string guess_mime(const std::string& file_path);
    /// 校验相对路径，防止目录遍历（..）
    static bool is_safe_relative_path(const std::string& rel);

    std::string root_dir_;
};

} // namespace solar_http
