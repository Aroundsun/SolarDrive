// ---------------------------------------------------------------------------
// rate_limiter.cpp
//
// Redis ZSET 滑动窗口限流实现。
// ---------------------------------------------------------------------------

#include "rate_limiter.h"

#include <algorithm>
#include <cctype>

namespace solar_cache {

namespace {

std::string trim_copy(const std::string& s) {
    size_t begin = 0;
    while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin]))) {
        ++begin;
    }
    size_t end = s.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(begin, end - begin);
}

} // namespace

RateLimiter::RateLimiter(std::shared_ptr<RedisClient> redis, int limit_per_minute)
    : redis_(std::move(redis))
    , limit_(limit_per_minute) {}

int64_t RateLimiter::now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

bool RateLimiter::is_exempt(const std::string& path) {
    if (path == "/" || path == "/index.html" || path == "/app.js" ||
        path == "/style.css" || path == "/favicon.ico" ||
        path == "/login.html" || path == "/auth.js" || path == "/session.js" ||
        path == "/receive.html" || path == "/receive.js" ||
        path == "/icons.js" ||
        path == "/metrics.html" || path == "/metrics.js" ||
        path == "/share.html" || path == "/share.js") {
        return true;
    }
    return false;
}

std::string RateLimiter::resolve_client_ip(const std::string& x_forwarded_for,
                                           const std::string& peer_ip) {
    if (!x_forwarded_for.empty()) {
        const auto comma = x_forwarded_for.find(',');
        const std::string first = trim_copy(
            comma == std::string::npos ? x_forwarded_for : x_forwarded_for.substr(0, comma));
        if (!first.empty()) {
            return first;
        }
    }
    return peer_ip;
}

bool RateLimiter::allow(const std::string& client_ip) {
    if (limit_ <= 0 || client_ip.empty()) {
        return true;
    }
    if (!redis_ || !redis_->connected()) {
        return true;
    }

    const int64_t ts = now_ms();
    const std::string member =
        std::to_string(ts) + ":" + std::to_string(seq_.fetch_add(1, std::memory_order_relaxed));
    const std::string key = "ratelimit:ip:" + client_ip;
    const int64_t window_ms = static_cast<int64_t>(kWindowSeconds) * 1000;

    return redis_->sliding_window_allow(key, ts, window_ms, limit_, member);
}

} // namespace solar_cache
