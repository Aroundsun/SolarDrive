// ---------------------------------------------------------------------------
// metrics.cpp
//
// Prometheus 指标实现：静态原子变量定义与文本格式序列化。
// ---------------------------------------------------------------------------

#include "metrics.h"
#include <sstream>

namespace solar_monitor {

// 静态成员初始化
std::atomic<uint64_t> Metrics::total_requests_{0};
std::atomic<uint64_t> Metrics::errors_{0};
std::atomic<int64_t>  Metrics::upload_bytes_{0};
std::atomic<int64_t>  Metrics::download_bytes_{0};
std::atomic<int64_t>  Metrics::active_connections_{0};

std::string Metrics::dump() {
    std::ostringstream oss;

    // counter: 累计 HTTP 请求数
    oss << "# HELP solardrive_requests_total Total HTTP requests processed\n";
    oss << "# TYPE solardrive_requests_total counter\n";
    oss << "solardrive_requests_total " << total_requests_.load(std::memory_order_relaxed) << "\n\n";

    // counter: 累计 HTTP 错误数
    oss << "# HELP solardrive_errors_total Total HTTP errors returned\n";
    oss << "# TYPE solardrive_errors_total counter\n";
    oss << "solardrive_errors_total " << errors_.load(std::memory_order_relaxed) << "\n\n";

    // counter: 累计上传字节
    oss << "# HELP solardrive_upload_bytes_total Total bytes uploaded\n";
    oss << "# TYPE solardrive_upload_bytes_total counter\n";
    oss << "solardrive_upload_bytes_total " << upload_bytes_.load(std::memory_order_relaxed) << "\n\n";

    // counter: 累计下载字节
    oss << "# HELP solardrive_download_bytes_total Total bytes downloaded\n";
    oss << "# TYPE solardrive_download_bytes_total counter\n";
    oss << "solardrive_download_bytes_total " << download_bytes_.load(std::memory_order_relaxed) << "\n\n";

    // gauge: 当前活跃连接数（可增可减）
    oss << "# HELP solardrive_active_connections Current active TCP connections\n";
    oss << "# TYPE solardrive_active_connections gauge\n";
    oss << "solardrive_active_connections " << active_connections_.load(std::memory_order_relaxed) << "\n";

    return oss.str();
}

std::string Metrics::dump_json() {
    std::ostringstream oss;
    oss << "{\"type\":\"metrics\","
        << "\"solardrive_requests_total\":" << total_requests_.load(std::memory_order_relaxed) << ","
        << "\"solardrive_errors_total\":" << errors_.load(std::memory_order_relaxed) << ","
        << "\"solardrive_upload_bytes_total\":" << upload_bytes_.load(std::memory_order_relaxed) << ","
        << "\"solardrive_download_bytes_total\":" << download_bytes_.load(std::memory_order_relaxed) << ","
        << "\"solardrive_active_connections\":" << active_connections_.load(std::memory_order_relaxed)
        << "}";
    return oss.str();
}

} // namespace solar_monitor
