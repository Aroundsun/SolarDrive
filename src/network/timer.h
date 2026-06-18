#pragma once

#include <atomic>
#include <cstdint>
#include <functional>

#include "timestamp.h"

namespace solar_net {

using TimerCallback = std::function<void()>;

class Timer {
public:
    Timer(TimerCallback cb, Timestamp when, double interval);
    ~Timer() = default;

    void run() const { callback_(); }

    Timestamp expiration() const { return expiration_; }
    bool repeat() const { return interval_ > 0.0; }
    int64_t sequence() const { return sequence_; }

    void restart(Timestamp now);

private:
    TimerCallback callback_;
    Timestamp expiration_;
    const double interval_;
    const int64_t sequence_;

    static std::atomic<int64_t> s_num_created_;
};

class TimerId {
public:
    TimerId() : timer_(nullptr), sequence_(0) {}

    TimerId(Timer* timer, int64_t seq)
        : timer_(timer)
        , sequence_(seq) {}

    friend class TimerQueue;

private:
    Timer* timer_;
    int64_t sequence_;
};

} // namespace solar_net
