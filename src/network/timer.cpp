// =============================================================================
// timer.cpp — Timer 构造与周期性 restart
// =============================================================================
#include "timer.h"

namespace solar_net {

std::atomic<int64_t> Timer::s_num_created_{0};

Timer::Timer(TimerCallback cb, Timestamp when, double interval)
    : callback_(std::move(cb))
    , expiration_(when)
    , interval_(interval)
    , sequence_(++s_num_created_) {}

void Timer::restart(Timestamp when) {
    expiration_ = add_time(when, interval_);
}

} // namespace solar_net
