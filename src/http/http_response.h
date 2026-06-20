// =============================================================================
// http_response.h — HTTP 响应构建与序列化
// SolarDrive HTTP 层：状态码、Header、JSON/二进制/下载响应
// =============================================================================
#pragma once

#include <string>
#include <unordered_map>
#include <sstream>
#include <cstdint>

namespace solar_http {

/// HTTP 响应对象，由 handler 填充后 serialize() 写回 socket
class HttpResponse {
public:
    HttpResponse() : status_code_(200), status_text_("OK") {}

    /// 设置 HTTP 状态码与原因短语
    void set_status(int code, const std::string& text) {
        status_code_ = code;
        status_text_ = text;
    }

    /// 设置单个响应头
    void set_header(const std::string& key, const std::string& val) {
        headers_[key] = val;
    }

    /// 设置文本/二进制 body，并自动写入 Content-Length
    void set_body(const std::string& body) {
        body_ = body;
        headers_["Content-Length"] = std::to_string(body_.size());
    }

    /// 设置 JSON body 及 Content-Type
    void set_json(const std::string& json) {
        set_body(json);
        headers_["Content-Type"] = "application/json; charset=utf-8";
    }

    /// 设置二进制 body 及 MIME 类型
    void set_binary(const std::string& data, const std::string& mime = "application/octet-stream") {
        body_ = data;
        headers_["Content-Type"] = mime;
        headers_["Content-Length"] = std::to_string(body_.size());
    }

    /// 统一错误响应：状态码 + JSON {"error":"..."}（安全序列化）
    void set_error(int code, const std::string& msg);

    /// 文件下载专用头：Content-Disposition + Content-Length
    void set_download_headers(const std::string& filename, size_t data_size) {
        headers_["Content-Disposition"] = "attachment; filename=\"" + filename + "\"";
        headers_["Content-Length"] = std::to_string(data_size);
    }

    /// 序列化为 HTTP/1.1 响应报文（含默认 Connection: close）
    std::string serialize() const {
        std::ostringstream oss;
        oss << "HTTP/1.1 " << status_code_ << " " << status_text_ << "\r\n";
        // 短连接，避免自定义 HTTP 解析器在 keep-alive 复用时出问题
        if (headers_.find("Connection") == headers_.end()) {
            oss << "Connection: close\r\n";
        }
        for (const auto& [k, v] : headers_) {
            oss << k << ": " << v << "\r\n";
        }
        oss << "\r\n" << body_;
        return oss.str();
    }

    int         status_code_;
    std::string status_text_;
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
};

} // namespace solar_http
