#include "ws_handler.h"

namespace solar_ws {

WsHandler::WsHandler(const TcpConnectionPtr& conn, const std::string& upload_id)
    : conn_(conn), upload_id_(upload_id) {}

size_t WsHandler::feed(const char* data, size_t len) {
    // 尝试解析一个 WebSocket 帧
    WsFrame frame;
    size_t consumed = WsCodec::decode(data, len, frame);
    if (consumed == 0)
        return 0;  // 帧不完整，等待更多数据

    // 处理控制帧
    switch (frame.opcode) {
        case OpCode::kPing:
            conn_->send(WsCodec::encode(OpCode::kPong, frame.payload));
            break;
        case OpCode::kClose:
            conn_->send(WsCodec::encode(OpCode::kClose, ""));
            // 连接由 TcpConnection 的析构清理
            break;
        default:
            break;
    }
    return consumed;
}

void WsHandler::send(const std::string& payload) {
    try {
        std::string frame = WsCodec::encode(OpCode::kText, payload);
        conn_->send(frame);
    } catch (...) {
        // 连接已断开，静默忽略
    }
}

} // namespace solar_ws
