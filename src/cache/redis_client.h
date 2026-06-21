// ---------------------------------------------------------------------------
// redis_client.h
//
// Redis 客户端封装（基于 hiredis）：提供字符串、集合、哈希及限流计数器操作。
// 用于秒传 hash 索引、用户/会话缓存、IP 限流等场景。
// ---------------------------------------------------------------------------

#pragma once

#include <string>
#include <optional>
#include <hiredis/hiredis.h>

namespace solar_cache {

// Redis 连接封装，构造时建立连接，析构时自动释放
class RedisClient {
public:
    RedisClient(const std::string& host, int port);
    ~RedisClient();

    RedisClient(const RedisClient&) = delete;
    RedisClient& operator=(const RedisClient&) = delete;

    bool connected() const { return ctx_ != nullptr; }

    // 连通性测试（PING -> PONG）
    bool ping();

    // ---- 字符串操作 ----
    // 写入键值，ttl_seconds > 0 时设置过期时间
    bool set(const std::string& key, const std::string& value, int ttl_seconds = -1);
    std::optional<std::string> get(const std::string& key);
    bool del(const std::string& key);

    // ---- 集合操作（秒传 hash 索引） ----
    bool sadd(const std::string& key, const std::string& member);
    bool sismember(const std::string& key, const std::string& member);

    // ---- 哈希操作（用户 / 会话缓存） ----
    bool hset(const std::string& key, const std::string& field, const std::string& value);
    std::optional<std::string> hget(const std::string& key, const std::string& field);
    bool expire(const std::string& key, int ttl_seconds);

    // 限流计数器：INCR 自增，首次创建时设置 TTL，返回当前计数值（失败返回 -1）
    int64_t incr(const std::string& key, int ttl_seconds);

    // 滑动窗口限流（ZSET + Lua）：窗口内未超限则记录本次请求并返回 true
    bool sliding_window_allow(const std::string& key,
                              int64_t now_ms,
                              int64_t window_ms,
                              int limit,
                              const std::string& member);

    // 主动断开 Redis 连接
    void disconnect();

private:
    redisContext* ctx_;  // hiredis 连接上下文，nullptr 表示未连接或已断开
};

} // namespace solar_cache
