#pragma once

#include <string>
#include <unordered_map>
#include <cstdint>

namespace solar_http {

// HTTP 方法枚举
enum class HttpMethod {
    GET,
    POST,
    PUT,
    DELETE,
    UNKNOWN
};

// 可读取的 HTTP 请求
// 由 HttpParser 解析完成后填充，传递给业务 Handler
class HttpRequest {
public:
    HttpRequest() : method_(HttpMethod::UNKNOWN), content_length_(0) {}

    void set_method(HttpMethod m) { method_ = m; }
    void set_path(const std::string& p) { path_ = p; }
    void set_query(const std::string& q) { query_ = q; }
    void set_version(const std::string& v) { version_ = v; }
    void add_header(const std::string& key, const std::string& val) { headers_[key] = val; }
    void append_body(const char* data, size_t len) { body_.append(data, len); }

    // 路径参数，由 HttpRouter::dispatch() 填充
    void clear_path_params() { path_params_.clear(); }
    void set_path_param(const std::string& key, const std::string& val) { path_params_[key] = val; }
    std::string get_path_param(const std::string& key) const {
        auto it = path_params_.find(key);
        return it != path_params_.end() ? it->second : "";
    }
    const std::unordered_map<std::string, std::string>& path_params() const { return path_params_; }

    HttpMethod method() const { return method_; }
    const std::string& path() const { return path_; }
    const std::string& query() const { return query_; }
    const std::string& version() const { return version_; }
    const std::string& body() const { return body_; }
    const std::unordered_map<std::string, std::string>& headers() const { return headers_; }

    std::string get_header(const std::string& key) const {
        auto it = headers_.find(key);
        return it != headers_.end() ? it->second : "";
    }

    size_t content_length() const {
        auto s = get_header("Content-Length");
        return s.empty() ? 0 : static_cast<size_t>(std::stoull(s));
    }

    void clear() {
        method_ = HttpMethod::UNKNOWN;
        path_.clear();
        query_.clear();
        version_.clear();
        headers_.clear();
        body_.clear();
        path_params_.clear();
        content_length_ = 0;
    }

private:
    HttpMethod method_;
    std::string path_;
    std::string query_;
    std::string version_;
    std::unordered_map<std::string, std::string> headers_;
    std::unordered_map<std::string, std::string> path_params_;
    std::string body_;
    size_t content_length_;
};

} // namespace solar_http
