#include "ws_session.h"
#include "websocket.h"
#include "../monitor/metrics.h"
#include "../network/tcp_connection.h"

#include <sstream>
#include <exception>
#include <algorithm>

namespace solar_ws {

WsSessionManager& WsSessionManager::instance() {
    static WsSessionManager inst;
    return inst;
}

void WsSessionManager::send_text_frame(const TcpConnectionPtr& conn, const std::string& json) {
    if (!conn) {
        return;
    }
    try {
        const std::string frame = WsCodec::encode(OpCode::kText, json, true);
        conn->send(frame);
    } catch (const std::exception&) {
        // 连接已断开
    }
}

void WsSessionManager::register_session(const std::string& upload_id, const TcpConnectionPtr& conn) {
    std::lock_guard<std::mutex> lock(mtx_);
    sessions_[upload_id] = conn;
}

void WsSessionManager::register_metrics(const TcpConnectionPtr& conn) {
    std::lock_guard<std::mutex> lock(mtx_);
    metrics_subscribers_.push_back(conn);
}

void WsSessionManager::remove_session(const TcpConnectionPtr& conn) {
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto it = sessions_.begin(); it != sessions_.end(); ) {
        if (it->second == conn) {
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }

    metrics_subscribers_.erase(
        std::remove(metrics_subscribers_.begin(), metrics_subscribers_.end(), conn),
        metrics_subscribers_.end());
}

void WsSessionManager::remove_session_by_id(const std::string& upload_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    sessions_.erase(upload_id);
}

void WsSessionManager::push_progress(const std::string& upload_id, int percent,
                                     int64_t bytes_uploaded, int64_t total_size) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = sessions_.find(upload_id);
    if (it == sessions_.end()) {
        return;
    }

    std::ostringstream oss;
    oss << "{\"type\":\"progress\","
        << "\"upload_id\":\"" << upload_id << "\","
        << "\"percent\":" << percent << ","
        << "\"bytes_uploaded\":" << bytes_uploaded << ","
        << "\"total_size\":" << total_size << "}";
    send_text_frame(it->second, oss.str());
}

void WsSessionManager::push_complete(const std::string& upload_id, const std::string& file_id) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = sessions_.find(upload_id);
    if (it == sessions_.end()) {
        return;
    }

    std::ostringstream oss;
    oss << "{\"type\":\"complete\","
        << "\"upload_id\":\"" << upload_id << "\","
        << "\"file_id\":\"" << file_id << "\","
        << "\"percent\":100}";
    send_text_frame(it->second, oss.str());
}

void WsSessionManager::push_metrics_snapshot(const TcpConnectionPtr& conn) {
    send_text_frame(conn, solar_monitor::Metrics::dump_json());
}

void WsSessionManager::broadcast_metrics() {
    const std::string json = solar_monitor::Metrics::dump_json();
    std::lock_guard<std::mutex> lock(mtx_);
    for (auto it = metrics_subscribers_.begin(); it != metrics_subscribers_.end(); ) {
        if (auto conn = *it) {
            try {
                send_text_frame(conn, json);
                ++it;
            } catch (const std::exception&) {
                it = metrics_subscribers_.erase(it);
            }
        } else {
            it = metrics_subscribers_.erase(it);
        }
    }
}

} // namespace solar_ws
