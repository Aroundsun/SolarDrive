#pragma once

#include <string>
#include <atomic>

namespace solar_monitor {

// Prometheus 监控指标收集器（线程安全）
class Metrics {
public:
    // 请求计数器
    static void inc_requests()                { total_requests_.fetch_add(1, std::memory_order_relaxed); }
    static void inc_upload_bytes(int64_t n)   { upload_bytes_.fetch_add(n, std::memory_order_relaxed); }
    static void inc_errors()                  { errors_.fetch_add(1, std::memory_order_relaxed); }
    static void inc_download_bytes(int64_t n) { download_bytes_.fetch_add(n, std::memory_order_relaxed); }
    static void inc_active_connections()      { active_connections_.fetch_add(1, std::memory_order_relaxed); }
    static void dec_active_connections()      { active_connections_.fetch_sub(1, std::memory_order_relaxed); }

    // 生成 Prometheus 格式的 /metrics 响应
    static std::string dump();

private:
    static std::atomic<uint64_t> total_requests_;
    static std::atomic<uint64_t> errors_;
    static std::atomic<int64_t>  upload_bytes_;
    static std::atomic<int64_t>  download_bytes_;
    static std::atomic<int64_t>  active_connections_;
};

} // namespace solar_monitor
