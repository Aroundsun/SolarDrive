#pragma once

#include "../ws/websocket.h"
#include "../ws/ws_session.h"
#include "../network/tcp_connection.h"
#include <memory>
#include <string>

namespace solar_ws {

// WebSocket 连接处理器
// 在 HTTP Upgrade 握手完成后替代 HttpParser 成为连接的上下文
class WsHandler {
public:
    WsHandler(const TcpConnectionPtr& conn, const std::string& upload_id);

    // 处理 WebSocket 帧数据（由 message_callback 调用）
    // 返回已消耗的字节数
    size_t feed(const char* data, size_t len);

    // 发送消息
    void send(const std::string& payload);

private:
    TcpConnectionPtr conn_;
    std::string upload_id_;
    solar_ws::WsFrame cur_frame_;
};

} // namespace solar_ws
