#include "cache/rate_limiter.h"
#include "cache/redis_client.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

namespace {

TEST(RateLimiterTest, ExemptStaticAssets) {
    EXPECT_TRUE(solar_cache::RateLimiter::is_exempt("/"));
    EXPECT_TRUE(solar_cache::RateLimiter::is_exempt("/app.js"));
    EXPECT_TRUE(solar_cache::RateLimiter::is_exempt("/login.html"));
    EXPECT_FALSE(solar_cache::RateLimiter::is_exempt("/api/v1/files"));
    EXPECT_FALSE(solar_cache::RateLimiter::is_exempt("/api/v1/auth/login"));
    EXPECT_FALSE(solar_cache::RateLimiter::is_exempt("/metrics"));
}

TEST(RateLimiterTest, ResolveClientIpUsesPeerWhenNoForwardedHeader) {
    EXPECT_EQ(
        solar_cache::RateLimiter::resolve_client_ip("", "192.168.1.10"),
        "192.168.1.10");
}

TEST(RateLimiterTest, ResolveClientIpUsesFirstForwardedForEntry) {
    EXPECT_EQ(
        solar_cache::RateLimiter::resolve_client_ip("203.0.113.1, 10.0.0.1", "127.0.0.1"),
        "203.0.113.1");
    EXPECT_EQ(
        solar_cache::RateLimiter::resolve_client_ip(" 198.51.100.2 ", "127.0.0.1"),
        "198.51.100.2");
}

TEST(RateLimiterTest, DisabledWhenLimitZero) {
    auto redis = std::make_shared<solar_cache::RedisClient>("127.0.0.1", 6379);
    solar_cache::RateLimiter limiter(redis, 0);
    for (int i = 0; i < 200; ++i) {
        EXPECT_TRUE(limiter.allow("127.0.0.1"));
    }
}

TEST(RateLimiterTest, FailOpenWhenRedisUnavailable) {
    solar_cache::RateLimiter limiter(std::make_shared<solar_cache::RedisClient>("127.0.0.1", 1),
                                     5);
    EXPECT_TRUE(limiter.allow("127.0.0.1"));
}

TEST(RateLimiterIntegration, EnforcesSlidingWindow) {
    if (!std::getenv("SOLAR_TEST_REDIS")) {
        GTEST_SKIP() << "set SOLAR_TEST_REDIS=1 to run Redis integration test";
    }

    auto redis = std::make_shared<solar_cache::RedisClient>("127.0.0.1", 6379);
    if (!redis->ping()) {
        GTEST_SKIP() << "Redis not reachable on 127.0.0.1:6379";
    }

    const std::string ip = "198.18.0.99";
    const std::string key = "ratelimit:ip:" + ip;
    redis->del(key);

    solar_cache::RateLimiter limiter(redis, 3);
    EXPECT_TRUE(limiter.allow(ip));
    EXPECT_TRUE(limiter.allow(ip));
    EXPECT_TRUE(limiter.allow(ip));
    EXPECT_FALSE(limiter.allow(ip));

    redis->del(key);
}

} // namespace
