#pragma once

#include <string>
#include <optional>
#include <hiredis/hiredis.h>

namespace solar_cache {

class RedisClient {
public:
    RedisClient(const std::string& host, int port);
    ~RedisClient();

    RedisClient(const RedisClient&) = delete;
    RedisClient& operator=(const RedisClient&) = delete;

    // 连通性测试
    bool ping();

    // 字符串操作
    bool set(const std::string& key, const std::string& value, int ttl_seconds = -1);
    std::optional<std::string> get(const std::string& key);
    bool del(const std::string& key);

    // 集合操作（秒传 hash 索引）
    bool sadd(const std::string& key, const std::string& member);
    bool sismember(const std::string& key, const std::string& member);

    // 哈希操作（用户 / 会话缓存）
    bool hset(const std::string& key, const std::string& field, const std::string& value);
    std::optional<std::string> hget(const std::string& key, const std::string& field);

    // 限流计数器
    int64_t incr(const std::string& key, int ttl_seconds);

    // 断开连接
    void disconnect();

private:
    redisContext* ctx_;
};

} // namespace solar_cache
