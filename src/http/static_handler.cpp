// =============================================================================
// static_handler.cpp — 静态资源服务实现
// SolarDrive HTTP 层：仅处理 GET 非 /api/ 请求，读取本地文件返回
// =============================================================================
#include "static_handler.h"

#include <fstream>
#include <sstream>

namespace solar_http {

StaticHandler::StaticHandler(std::string root_dir)
    : root_dir_(std::move(root_dir))
{
    // 统一去掉末尾 /，拼接路径时始终用 root + "/" + rel
    if (!root_dir_.empty() && root_dir_.back() == '/') {
        root_dir_.pop_back();
    }
}

std::string StaticHandler::map_url_to_file(const std::string& path) {
    if (path.empty() || path == "/") {
        return "index.html";
    }
    if (path.front() == '/') {
        return path.substr(1);
    }
    return path;
}

std::string StaticHandler::guess_mime(const std::string& file_path) {
    const auto dot = file_path.rfind('.');
    if (dot == std::string::npos) {
        return "application/octet-stream";
    }
    const std::string ext = file_path.substr(dot);
    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    if (ext == ".css")  return "text/css; charset=utf-8";
    if (ext == ".js")   return "application/javascript; charset=utf-8";
    if (ext == ".json") return "application/json; charset=utf-8";
    if (ext == ".svg")  return "image/svg+xml";
    if (ext == ".png")  return "image/png";
    if (ext == ".ico")  return "image/x-icon";
    return "application/octet-stream";
}

bool StaticHandler::is_safe_relative_path(const std::string& rel) {
    if (rel.empty()) return false;
    if (rel.front() == '/') return false;
    if (rel.find("..") != std::string::npos) return false;
    return true;
}

bool StaticHandler::try_serve(const HttpRequest& req, HttpResponse& resp) const {
    // 仅处理 GET；API 路由交给 HttpRouter
    if (req.method() != HttpMethod::GET) {
        return false;
    }

    const std::string& path = req.path();
    if (path.rfind("/api/", 0) == 0) {
        return false;
    }

    const std::string rel = map_url_to_file(path);
    if (!is_safe_relative_path(rel)) {
        return false;
    }

    const std::string full_path = root_dir_ + "/" + rel;
    std::ifstream in(full_path, std::ios::binary);
    if (!in) {
        return false;
    }

    std::ostringstream oss;
    oss << in.rdbuf();
    resp.set_status(200, "OK");
    resp.set_body(oss.str());
    resp.set_header("Content-Type", guess_mime(rel));
    resp.set_header("Cache-Control", "no-cache");
    return true;
}

} // namespace solar_http
