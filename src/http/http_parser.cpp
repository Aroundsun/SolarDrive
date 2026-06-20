// =============================================================================
// http_parser.cpp — HTTP/1.1 请求解析器实现
// SolarDrive HTTP 层：状态机解析、keep-alive 复用、HTTP pipelining
// =============================================================================
#include "http_parser.h"
#include "http_request.h"
#include <cctype>
#include <algorithm>
#include <sstream>

namespace solar_http {

// 将字符串转换为小写
static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

// 解析 HTTP 方法
static HttpMethod parse_method(const std::string& m) {
    auto low = to_lower(m);
    if (low == "get")    return HttpMethod::GET;
    if (low == "post")   return HttpMethod::POST;
    if (low == "put")    return HttpMethod::PUT;
    if (low == "delete") return HttpMethod::DELETE;
    return HttpMethod::UNKNOWN;
}

// ---- HttpParser 实现 ----

HttpParser::HttpParser(RequestCallback cb)
    : state_(State::kStart)
    , callback_(std::move(cb))
    , body_bytes_remaining_(0)
    , complete_(false)
{
    reset_request();
}

std::size_t HttpParser::feed(const char* data, std::size_t len) {
    std::size_t consumed = 0;
    for (std::size_t i = 0; i < len; ++i) {
        parse_char(data[i]);
        consumed++;
        if (complete_) {
            fire_callback();
            reset();
            // 支持 pipelining：继续解析同一个 buffer 里的下一个请求
        }
    }
    return consumed;
}

void HttpParser::reset() {
    state_ = State::kStart;
    complete_ = false;
    body_bytes_remaining_ = 0;
    method_buf_.clear();
    path_buf_.clear();
    query_buf_.clear();
    version_buf_.clear();
    cur_header_value_.clear();
    body_buf_.clear();
    reset_request();
}

void HttpParser::reset_request() {
    cur_req_ = std::make_unique<HttpRequest>();
    cur_header_field_.clear();
}

void HttpParser::parse_char(char c) {
    switch (state_) {

    case State::kStart: {
        if (std::isspace(c)) return;
        method_buf_.clear();
        method_buf_.push_back(c);
        state_ = State::kMethod;
        break;
    }

    case State::kMethod: {
        if (c == ' ') {
            cur_req_->set_method(parse_method(method_buf_));
            state_ = State::kBeforePath;
        } else {
            method_buf_.push_back(c);
        }
        break;
    }

    case State::kBeforePath: {
        if (c == ' ') return; // 跳过空格
        path_buf_.clear();
        path_buf_.push_back(c);
        state_ = State::kPath;
        break;
    }

    case State::kPath: {
        if (c == '?') {
            state_ = State::kQuery;
        } else if (c == ' ') {
            cur_req_->set_path(path_buf_);
            state_ = State::kVersionH;
        } else {
            path_buf_.push_back(c);
        }
        break;
    }

    case State::kQuery: {
        if (c == ' ') {
            cur_req_->set_path(path_buf_);
            cur_req_->set_query(query_buf_);
            state_ = State::kVersionH;
        } else {
            query_buf_.push_back(c);
        }
        break;
    }

    case State::kVersionH: {
        if (c == 'H') { version_buf_.clear(); version_buf_.push_back(c); state_ = State::kVersionT1; }
        else if (!std::isspace(c)) { /* junk */ }
        break;
    }
    case State::kVersionT1: {
        if (c == 'T') { version_buf_.push_back(c); state_ = State::kVersionSlash; }
        break;
    }
    case State::kVersionSlash: {
        if (c == '/') { version_buf_.push_back(c); state_ = State::kVersionMajor; }
        break;
    }
    case State::kVersionMajor: {
        if (c == '.') { version_buf_.push_back(c); state_ = State::kVersionMinor; }
        else { version_buf_.push_back(c); }
        break;
    }
    case State::kVersionMinor: {
        if (c == '\r') { version_buf_.push_back(c); state_ = State::kAfterVersion; }
        else if (c == '\n') {
            cur_req_->set_version("HTTP/1.1"); // 简略
            state_ = State::kHeaderLF;
        } else {
            version_buf_.push_back(c);
        }
        break;
    }
    case State::kAfterVersion: {
        if (c == '\n') {
            cur_req_->set_version("HTTP/1.1");
            state_ = State::kHeaderLF;
        }
        break;
    }

    case State::kHeaderLF: {
        if (c == '\r') { state_ = State::kHeaderFieldStart; break; }
        if (c == '\n') {
            // 无 header，直接跳到 body
            transition_to_body();
            break;
        }
        // 首字母
        cur_header_field_.clear();
        cur_header_field_.push_back(c);
        state_ = State::kHeaderField;
        break;
    }

    case State::kHeaderFieldStart: {
        if (c == '\n') {
            // 空行，headers 结束
            transition_to_body();
            break;
        }
        cur_header_field_.clear();
        cur_header_field_.push_back(c);
        state_ = State::kHeaderField;
        break;
    }

    case State::kHeaderField: {
        if (c == ':') {
            state_ = State::kHeaderBeforeValue;
        } else {
            cur_header_field_.push_back(c);
        }
        break;
    }

    case State::kHeaderBeforeValue: {
        if (c == ' ') break; // 跳过前置空格
        std::string val;
        val.push_back(c);
        cur_header_value_.clear();
        cur_header_value_ = val;
        state_ = State::kHeaderValue;
        break;
    }

    case State::kHeaderValue: {
        if (c == '\r') {
            cur_req_->add_header(cur_header_field_, cur_header_value_);
            state_ = State::kHeaderValueLF;
        } else if (c == '\n') {
            // 非标准但有些客户端这么发
            cur_req_->add_header(cur_header_field_, cur_header_value_);
            state_ = State::kHeaderLF;
        } else {
            cur_header_value_.push_back(c);
        }
        break;
    }

    case State::kHeaderValueLF: {
        if (c == '\n') {
            state_ = State::kHeaderLF;
        }
        break;
    }

    case State::kBody: {
        body_buf_.push_back(c);
        if (body_buf_.size() >= body_bytes_remaining_) {
            cur_req_->append_body(body_buf_.data(), body_buf_.size());
            complete_ = true;
        }
        break;
    }

    default: break;
    }
}

void HttpParser::transition_to_body() {
    // 检查是否有 Content-Length
    std::string cl = cur_req_->get_header("Content-Length");
    if (!cl.empty()) {
        body_bytes_remaining_ = static_cast<std::size_t>(std::stoull(cl));
        if (body_bytes_remaining_ > 0) {
            body_buf_.clear();
            state_ = State::kBody;
            return;
        }
    }
    // 无 body 或 body 长度为 0
    complete_ = true;
}

void HttpParser::fire_callback() {
    if (callback_) {
        callback_(*cur_req_);
    }
}

} // namespace solar_http
