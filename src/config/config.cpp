// ---------------------------------------------------------------------------
// config.cpp
//
// 配置加载实现：解析 YAML 配置文件，按节映射到 AppConfig 各字段。
// ---------------------------------------------------------------------------

#include "config.h"
#include <yaml-cpp/yaml.h>
#include <iostream>

namespace solar_config {

std::string DbConfig::conn_str() const {
    return "host=" + host + " port=" + std::to_string(port)
         + " dbname=" + dbname + " user=" + user + " password=" + password;
}

AppConfig AppConfig::load_from_file(const std::string& path) {
    AppConfig cfg;

    try {
        YAML::Node root = YAML::LoadFile(path);

        // HTTP 服务
        if (auto node = root["server"]) {
            if (node["port"])             cfg.server.port            = node["port"].as<uint16_t>();
            if (node["threads"])          cfg.server.threads         = node["threads"].as<int>();
            if (node["max_connections"])  cfg.server.max_connections = node["max_connections"].as<int>();
        }

        // 对象存储
        if (auto node = root["storage"]) {
            if (node["base_path"])  cfg.storage.base_path  = node["base_path"].as<std::string>();
            if (node["chunk_size"]) cfg.storage.chunk_size = node["chunk_size"].as<size_t>();
        }

        // 数据库
        if (auto node = root["database"]) {
            if (node["host"])     cfg.database.host     = node["host"].as<std::string>();
            if (node["port"])     cfg.database.port     = node["port"].as<uint16_t>();
            if (node["dbname"])   cfg.database.dbname   = node["dbname"].as<std::string>();
            if (node["user"])     cfg.database.user     = node["user"].as<std::string>();
            if (node["password"]) cfg.database.password = node["password"].as<std::string>();
            if (node["pool_size"])cfg.database.pool_size= node["pool_size"].as<int>();
        }

        // Redis 缓存
        if (auto node = root["redis"]) {
            if (node["host"])     cfg.redis.host     = node["host"].as<std::string>();
            if (node["port"])     cfg.redis.port     = node["port"].as<uint16_t>();
            if (node["password"]) cfg.redis.password = node["password"].as<std::string>();
        }

        // JWT 认证
        if (auto node = root["jwt"]) {
            if (node["secret"])    cfg.jwt.secret    = node["secret"].as<std::string>();
            if (node["ttl_hours"]) cfg.jwt.ttl_hours = node["ttl_hours"].as<int>();
        }

        // 日志
        if (auto node = root["logging"]) {
            if (node["level"])        cfg.logging.level       = node["level"].as<std::string>();
            if (node["file"])         cfg.logging.file        = node["file"].as<std::string>();
            if (node["max_size_mb"])  cfg.logging.max_size_mb = node["max_size_mb"].as<int>();
            if (node["max_files"])    cfg.logging.max_files   = node["max_files"].as<int>();
        }

        // 限流与配额
        if (auto node = root["limits"]) {
            if (node["rate_limit_per_ip"])        cfg.limits.rate_limit_per_ip      = node["rate_limit_per_ip"].as<int>();
            if (node["max_file_size_mb"])         cfg.limits.max_file_size_mb       = node["max_file_size_mb"].as<int>();
            if (node["storage_quota_per_user_gb"]) cfg.limits.storage_quota_per_user_gb = node["storage_quota_per_user_gb"].as<int>();
        }

    } catch (const YAML::Exception& e) {
        // YAML 解析失败：降级为默认配置，不中断启动
        std::cerr << "[config] WARN: failed to load '" << path
                  << "': " << e.what() << "\n"
                  << "[config] Using default configuration.\n";
    } catch (const std::exception& e) {
        std::cerr << "[config] WARN: " << e.what()
                  << "\n[config] Using default configuration.\n";
    }

    return cfg;
}

} // namespace solar_config
