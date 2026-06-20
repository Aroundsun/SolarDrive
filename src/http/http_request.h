// =============================================================================
// http_request.h — HTTP 请求数据结构
// SolarDrive HTTP 层：由 HttpParser 填充，供 Router 与 Handler 使用
// =============================================================================
#pragma once

#include <string>
#include <unordered_map>
#include <cstdint>
#include <algorithm>
#include <cctype>

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

    // ---- 解析器写入接口 ----
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

    // ---- 只读访问接口 ----
    HttpMethod method() const { return method_; }
    const std::string& path() const { return path_; }
    const std::string& query() const { return query_; }
    const std::string& version() const { return version_; }
    const std::string& body() const { return body_; }
    const std::unordered_map<std::string, std::string>& headers() const { return headers_; }

    /// 按 key 取 header，不存在返回空串
    std::string get_header(const std::string& key) const {
        auto it = headers_.find(key);
        return it != headers_.end() ? it->second : "";
    }

    /// 按 key 取 header（大小写不敏感）
    std::string get_header_ic(const std::string& key) const {
        for (const auto& [k, v] : headers_) {
            if (k.size() == key.size() &&
                std::equal(k.begin(), k.end(), key.begin(),
                           [](unsigned char a, unsigned char b) {
                               return std::tolower(a) == std::tolower(b);
                           })) {
                return v;
            }
        }
        return "";
    }

    /// 客户端是否期望持久连接（HTTP/1.1 默认 keep-alive，除非 Connection: close）
    bool wants_keep_alive() const;

    /// 从 Content-Length 头解析 body 长度
    size_t content_length() const {
        auto s = get_header("Content-Length");
        return s.empty() ? 0 : static_cast<size_t>(std::stoull(s));
    }

    /// 重置为初始状态（连接复用时）
    void clear() {
        method_ = HttpMethod::UNKNOWN;
        path_.clear();
        query_.clear();
        version_.clear();
        headers_.clear();
        body_.clear();
        path_params_.clear();
        content_length_ = 0;
        auth_user_id_.clear();
        auth_username_.clear();
    }

    // ---- 鉴权上下文（由 main 在路由前写入，Handler 只读） ----
    void set_auth_user(const std::string& user_id, const std::string& username) {
        auth_user_id_ = user_id;
        auth_username_ = username;
    }
    bool has_auth_user() const { return !auth_user_id_.empty(); }
    const std::string& auth_user_id() const { return auth_user_id_; }
    const std::string& auth_username() const { return auth_username_; }

private:
    HttpMethod method_;
    std::string path_;
    std::string query_;
    std::string version_;
    std::unordered_map<std::string, std::string> headers_;
    std::unordered_map<std::string, std::string> path_params_;
    std::string body_;
    size_t content_length_;
    std::string auth_user_id_;
    std::string auth_username_;
};

} // namespace solar_http
