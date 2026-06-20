// ---------------------------------------------------------------------------
// config.h
//
// 应用配置定义：各子系统配置结构体及 YAML 加载入口。
// 未在配置文件中指定的字段保留编译期默认值。
// ---------------------------------------------------------------------------

#pragma once

#include <string>
#include <cstdint>

namespace solar_config {

// PostgreSQL 数据库连接配置
struct DbConfig {
    std::string host     = "127.0.0.1";
    uint16_t    port     = 5432;
    std::string dbname   = "solardrive";
    std::string user     = "postgres";
    std::string password = "postgres";
    int         pool_size = 8;

    // 生成 libpq 连接字符串
    std::string conn_str() const;
};

// Redis 缓存配置
struct RedisConfig {
    std::string host     = "127.0.0.1";
    uint16_t    port     = 6379;
    std::string password = "";
};

// HTTP 服务配置
struct ServerConfig {
    uint16_t port            = 8080;
    int      threads         = 4;       // 工作线程数
    int      max_connections = 10000;   // 最大并发连接数
};

// 对象存储配置
struct StorageConfig {
    std::string base_path  = "/tmp/solardrive/objects";  // 文件块存储根目录
    size_t      chunk_size = 4 * 1024 * 1024;            // 分块大小 4MB
};

// JWT 认证配置
struct JwtConfig {
    std::string secret    = "solardrive-dev-secret";
    int         ttl_hours = 168;  // Token 有效期（小时），默认 7 天
};

// 日志配置
struct LogConfig {
    std::string level       = "info";
    std::string file        = "logs/solardrive.log";
    int         max_size_mb = 10;   // 单文件最大体积（MB）
    int         max_files   = 5;    // 滚动保留文件数
};

// 业务限流与配额配置
struct LimitsConfig {
    int rate_limit_per_ip         = 100;    // 每 IP 每分钟请求上限
    int max_file_size_mb          = 10240;  // 单文件大小上限（MB）
    int storage_quota_per_user_gb = 10;     // 每用户默认存储配额（GB）

    int64_t max_file_size_bytes() const {
        return static_cast<int64_t>(max_file_size_mb) * 1024 * 1024;
    }
};

// 全局应用配置（聚合各子系统配置）
struct AppConfig {
    ServerConfig  server;
    DbConfig      database;
    RedisConfig   redis;
    JwtConfig     jwt;
    LogConfig     logging;
    StorageConfig storage;
    LimitsConfig  limits;

    // 从 YAML 文件加载配置，路径默认 config/config.yaml
    // 加载失败时打印警告并返回默认值
    static AppConfig load_from_file(const std::string& path = "config/config.yaml");
};

} // namespace solar_config
