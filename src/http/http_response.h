#pragma once

#include <string>
#include <unordered_map>
#include <sstream>
#include <cstdint>

namespace solar_http {

class HttpResponse {
public:
    HttpResponse() : status_code_(200), status_text_("OK") {}

    void set_status(int code, const std::string& text) {
        status_code_ = code;
        status_text_ = text;
    }

    void set_header(const std::string& key, const std::string& val) {
        headers_[key] = val;
    }

    void set_body(const std::string& body) {
        body_ = body;
        headers_["Content-Length"] = std::to_string(body_.size());
    }

    void set_json(const std::string& json) {
        set_body(json);
        headers_["Content-Type"] = "application/json; charset=utf-8";
    }

    void set_binary(const std::string& data, const std::string& mime = "application/octet-stream") {
        body_ = data;
        headers_["Content-Type"] = mime;
        headers_["Content-Length"] = std::to_string(body_.size());
    }

    void set_error(int code, const std::string& msg) {
        status_code_ = code;
        status_text_ = msg;
        set_json("{\"error\":\"" + msg + "\"}");
    }

    void set_download_headers(const std::string& filename, size_t data_size) {
        headers_["Content-Disposition"] = "attachment; filename=\"" + filename + "\"";
        headers_["Content-Length"] = std::to_string(data_size);
    }

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
