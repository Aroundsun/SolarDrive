#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <memory>
#include <mutex>

// 前置声明
namespace solar_net { class TcpConnection; }
using TcpConnectionPtr = std::shared_ptr<solar_net::TcpConnection>;

namespace solar_ws {

// WebSocket 会话管理器
// 管理所有 WebSocket 连接，支持按 upload_id 推送消息
class WsSessionManager {
public:
    static WsSessionManager& instance();

    // 注册一个 WebSocket 连接（关联到 upload_id）
    void register_session(const std::string& upload_id, const TcpConnectionPtr& conn);

    // 移除连接（断开时调用）
    void remove_session(const TcpConnectionPtr& conn);
    void remove_session_by_id(const std::string& upload_id);

    // 向指定 upload_id 推送上传进度
    void push_progress(const std::string& upload_id, int percent, int64_t bytes_uploaded, int64_t total_size);

    // 推送上传完成通知
    void push_complete(const std::string& upload_id, const std::string& file_id);

    // 注册 / 移除监控 WebSocket 订阅者
    void register_metrics(const TcpConnectionPtr& conn);
    void push_metrics_snapshot(const TcpConnectionPtr& conn);
    void broadcast_metrics();

private:
    WsSessionManager() = default;

    void send_text_frame(const TcpConnectionPtr& conn, const std::string& json);

    std::unordered_map<std::string, TcpConnectionPtr> sessions_;
    std::vector<TcpConnectionPtr> metrics_subscribers_;
    std::mutex mtx_;
};

} // namespace solar_ws
