// ---------------------------------------------------------------------------
// metrics.h
//
// Prometheus 监控指标收集器：线程安全的原子计数器，供 /metrics 端点导出。
// ---------------------------------------------------------------------------

#pragma once

#include <string>
#include <atomic>

namespace solar_monitor {

// 全局指标收集器（全部为静态原子变量，无实例化）
class Metrics {
public:
    // ---- 计数器递增 ----
    static void inc_requests()                { total_requests_.fetch_add(1, std::memory_order_relaxed); }
    static void inc_upload_bytes(int64_t n)   { upload_bytes_.fetch_add(n, std::memory_order_relaxed); }
    static void inc_errors()                  { errors_.fetch_add(1, std::memory_order_relaxed); }
    static void inc_download_bytes(int64_t n) { download_bytes_.fetch_add(n, std::memory_order_relaxed); }
    static void inc_active_connections()      { active_connections_.fetch_add(1, std::memory_order_relaxed); }
    static void dec_active_connections()      { active_connections_.fetch_sub(1, std::memory_order_relaxed); }

    // 序列化为 Prometheus 文本 exposition 格式
    static std::string dump();

    // 序列化为 JSON，供 WebSocket /ws/metrics 推送
    static std::string dump_json();

private:
    static std::atomic<uint64_t> total_requests_;       // HTTP 请求总数（counter）
    static std::atomic<uint64_t> errors_;               // HTTP 错误总数（counter）
    static std::atomic<int64_t>  upload_bytes_;          // 累计上传字节（counter）
    static std::atomic<int64_t>  download_bytes_;        // 累计下载字节（counter）
    static std::atomic<int64_t>  active_connections_;   // 当前活跃 TCP 连接数（gauge）
};

} // namespace solar_monitor
