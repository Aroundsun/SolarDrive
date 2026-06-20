// =============================================================================
// http_parser.h — HTTP/1.1 请求解析器
// SolarDrive HTTP 层：逐字节状态机，解析方法/路径/头/body
// =============================================================================
#pragma once

#include "http_request.h"

#include <memory>
#include <string>
#include <any>
#include <functional>

namespace solar_http {

/// 完整请求解析完成后的回调（非 const，便于写入鉴权上下文）
using RequestCallback = std::function<void(HttpRequest&)>;

/// 轻量 HTTP/1.1 请求解析器
/// 逐字节状态机，从 solar_net::Buffer 风格的数据中解析
/// 支持：GET/POST/DELETE、Content-Length body、Connection: keep-alive
class HttpParser {
public:
    explicit HttpParser(RequestCallback cb);
    ~HttpParser() = default;

    /// 喂数据，返回消耗字节数（实际为处理掉的字节数，目前为同步消费）
    /// 每解析出一个完整请求，调用一次 callback
    std::size_t feed(const char* data, std::size_t len);

    /// 重置状态（keep-alive 连接复用）
    void reset();

    /// 获取当前解析中的请求（callback 里直接用）
    HttpRequest& request() { return *cur_req_; }
    const HttpRequest& request() const { return *cur_req_; }

private:
    /// 解析状态机各阶段
    enum class State {
        kStart,
        kMethod,
        kBeforePath,
        kPath,
        kQuery,
        kVersionH,
        kVersionT1,
        kVersionSlash,
        kVersionMajor,
        kVersionDot,
        kVersionMinor,
        kAfterVersion,
        kHeaderLF,
        kHeaderFieldStart,
        kHeaderField,
        kHeaderBeforeValue,
        kHeaderValue,
        kHeaderValueLF,
        kBody,
        kDone
    };

    /// 处理单个输入字节，驱动状态转移
    void parse_char(char c);
    /// headers 结束：根据 Content-Length 进入 body 或标记完成
    void transition_to_body();
    /// 触发 callback，将 cur_req_ 交给上层
    void fire_callback();
    /// 新建 HttpRequest，清空当前 header 缓冲
    void reset_request();

    State            state_;
    std::unique_ptr<HttpRequest> cur_req_;
    std::string      cur_header_field_;
    std::string      cur_header_value_;
    std::string      body_buf_;
    RequestCallback  callback_;
    std::size_t     body_bytes_remaining_;
    bool             complete_;
    std::string      method_buf_;
    std::string      path_buf_;
    std::string      query_buf_;
    std::string      version_buf_;
};

} // namespace solar_http
