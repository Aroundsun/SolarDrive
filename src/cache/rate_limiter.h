// ---------------------------------------------------------------------------
// rate_limiter.h
//
// 基于 Redis 滑动窗口的 per-IP 限流（配置 limits.rate_limit_per_ip，默认 100/min）。
// ---------------------------------------------------------------------------

#pragma once

#include "redis_client.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace solar_cache {

class RateLimiter {
public:
    static constexpr int kWindowSeconds = 60;

    RateLimiter(std::shared_ptr<RedisClient> redis, int limit_per_minute);

    // 是否允许该 IP 继续请求；Redis 不可用时 fail-open（放行）
    bool allow(const std::string& client_ip);

    // 静态资源等路径不参与限流
    static bool is_exempt(const std::string& path);

    // 优先 X-Forwarded-For 首段，否则使用 TCP 对端 IP
    static std::string resolve_client_ip(const std::string& x_forwarded_for,
                                         const std::string& peer_ip);

    int limit() const { return limit_; }

private:
    static int64_t now_ms();

    std::shared_ptr<RedisClient> redis_;
    int limit_;
    std::atomic<uint64_t> seq_{0};
};

} // namespace solar_cache
