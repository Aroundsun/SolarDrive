// =============================================================================
// http_response.h — HTTP 响应构建与序列化
// SolarDrive HTTP 层：状态码、Header、JSON/二进制/下载响应
// =============================================================================
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <sstream>
#include <cstdint>

namespace solar_net {
class TcpConnection;
}

namespace solar_http {

/// HTTP 响应对象，由 handler 填充后 serialize() 写回 socket
class HttpResponse {
public:
    HttpResponse() : status_code_(200), status_text_("OK"), close_connection_(true) {}

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

    /// 设置是否在响应后关闭 TCP 连接（默认 close）
    void set_close_connection(bool close) { close_connection_ = close; }
    bool close_connection() const { return close_connection_; }

    /// 序列化为 HTTP/1.1 响应报文
    std::string serialize() const {
        std::ostringstream oss;
        oss << "HTTP/1.1 " << status_code_ << " " << status_text_ << "\r\n";
        if (headers_.find("Connection") == headers_.end()) {
            oss << (close_connection_ ? "Connection: close\r\n" : "Connection: keep-alive\r\n");
        }
        if (!close_connection_ && headers_.find("Keep-Alive") == headers_.end()) {
            oss << "Keep-Alive: timeout=60, max=1000\r\n";
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

private:
    bool close_connection_;
};

/// 发送 HTTP 响应；keep_alive=true 时保持连接供后续请求复用
void send_response(const std::shared_ptr<solar_net::TcpConnection>& conn,
                   HttpResponse& resp,
                   bool keep_alive);

/// 发送 HTTP 响应并在写完后关闭连接（Connection: close）
void send_response_and_close(const std::shared_ptr<solar_net::TcpConnection>& conn,
                             HttpResponse& resp);

} // namespace solar_http
