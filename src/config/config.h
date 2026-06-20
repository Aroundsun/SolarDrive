#pragma once

#include <string>
#include <cstdint>

namespace solar_config {

// 数据库配置
struct DbConfig {
    std::string host     = "127.0.0.1";
    uint16_t    port     = 5432;
    std::string dbname   = "solardrive";
    std::string user     = "postgres";
    std::string password = "postgres";
    int         pool_size = 8;

    std::string conn_str() const;
};

// Redis 配置
struct RedisConfig {
    std::string host     = "127.0.0.1";
    uint16_t    port     = 6379;
    std::string password = "";
};

// 服务配置
struct ServerConfig {
    uint16_t port            = 8080;
    int      threads         = 4;
    int      max_connections = 10000;
};

// 存储配置
struct StorageConfig {
    std::string base_path  = "/tmp/solardrive/objects";
    size_t      chunk_size = 4 * 1024 * 1024;  // 4MB
};

// JWT 配置
struct JwtConfig {
    std::string secret    = "solardrive-dev-secret";
    int         ttl_hours = 168;
};

// 日志配置
struct LogConfig {
    std::string level       = "info";
    std::string file        = "logs/solardrive.log";
    int         max_size_mb = 10;
    int         max_files   = 5;
};

// 限流配置
struct LimitsConfig {
    int rate_limit_per_ip      = 100;
    int max_file_size_mb       = 10240;
    int storage_quota_per_user_gb = 10;
};

// 全局配置（单例）
struct AppConfig {
    ServerConfig  server;
    DbConfig      database;
    RedisConfig   redis;
    JwtConfig     jwt;
    LogConfig     logging;
    StorageConfig storage;
    LimitsConfig  limits;

    // 从 YAML 文件加载配置，未指定的字段保留默认值
    static AppConfig load_from_file(const std::string& path = "config/config.yaml");
};

} // namespace solar_config
