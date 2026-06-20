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
    static std::string map_url_to_file(const std::string& path);
    static std::string guess_mime(const std::string& file_path);
    static bool is_safe_relative_path(const std::string& rel);

    std::string root_dir_;
};

} // namespace solar_http
