// =============================================================================
// timer_queue.cpp — timerfd 驱动定时器到期与取消逻辑
// =============================================================================
#include "timer_queue.h"

#include "channel.h"
#include "event_loop.h"
#include "log.h"

#include <sys/timerfd.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>
#include <cstring>

namespace solar_net {

namespace {

struct timespec time_from_now(Timestamp when) {
    Timestamp current = now();
    int64_t usec = 100;
    if (when > current) {
        usec = std::chrono::duration_cast<std::chrono::microseconds>(when - current).count();
    }

    struct timespec ts{};
    ts.tv_sec = static_cast<time_t>(usec / 1000000);
    ts.tv_nsec = static_cast<long>((usec % 1000000) * 1000);
    return ts;
}

struct timespec duration_to_spec(int ms) {
    struct timespec ts{};
    if (ms <= 0) {
        ts.tv_nsec = 1;
        return ts;
    }

    ts.tv_sec = ms / 1000;
    ts.tv_nsec = static_cast<long>(ms % 1000) * 1000000L;
    return ts;
}

} // namespace

TimerQueue::TimerQueue(EventLoop* loop)
    : loop_(loop)
    , timerfd_(::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)) {
    if (timerfd_ < 0) {
        SNLOG_CRITICAL("TimerQueue: timerfd_create failed, errno={}", errno);
        ::abort();
    }

    timerfd_channel_ = std::make_unique<Channel>(loop_, timerfd_);
    timerfd_channel_->set_read_callback([this]() { handle_read(); });
    timerfd_channel_->enable_reading();
}

TimerQueue::~TimerQueue() {
    timerfd_channel_->disable_all();
    timerfd_channel_->remove();

    for (const auto& entry : timers_) {
        delete entry.second;
    }
    timers_.clear();

    for (Timer* timer : pending_cancel_timers_) {
        delete timer;
    }
    pending_cancel_timers_.clear();

    for (Timer* timer : canceling_timers_) {
        delete timer;
    }
    canceling_timers_.clear();

    ::close(timerfd_);
}

TimerId TimerQueue::add_timer(TimerCallback cb, Timestamp when, double interval) {
    auto* timer = new Timer(std::move(cb), when, interval);
    loop_->run_in_loop([this, timer]() { add_timer_in_loop(timer); });
    return TimerId(timer, timer->sequence());
}

void TimerQueue::cancel(TimerId timer_id) {
    loop_->run_in_loop([this, timer_id]() { cancel_in_loop(timer_id); });
}

int TimerQueue::get_next_timeout_ms() const {
    loop_->assert_in_loop_thread();

    if (timers_.empty()) {
        return EventLoop::kPollTimeoutMs;
    }

    Timestamp current = now();
    Timestamp next = timers_.begin()->first;
    if (next <= current) {
        return 0;
    }

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(next - current).count();
    return static_cast<int>(ms > 0 ? ms : 0);
}

void TimerQueue::handle_read() {
    loop_->assert_in_loop_thread();

    Timestamp current = now();
    uint64_t howmany = 0;
    ssize_t n = ::read(timerfd_, &howmany, sizeof(howmany));
    if (n != sizeof(howmany)) {
        SNLOG_ERROR("TimerQueue: read timerfd failed, errno={}", errno);
    }

    std::vector<Timer*> expired = get_expired(current);

    calling_expired_timers_ = true;
    for (Timer* timer : expired) {
        if (canceling_timers_.find(timer) == canceling_timers_.end()) {
            timer->run();
        }

        if (timer->repeat() && canceling_timers_.find(timer) == canceling_timers_.end()) {
            timer->restart(current);
            insert(timer);
        } else {
            delete timer;
        }
    }
    // 回调期间 cancel 的定时器延后在此统一 erase + delete，避免迭代中修改 set
    calling_expired_timers_ = false;

    for (Timer* timer : canceling_timers_) {
        auto it = timers_.find(TimerEntry(timer->expiration(), timer));
        if (it != timers_.end()) {
            timers_.erase(it);
        }
        delete timer;
    }
    canceling_timers_.clear();

    if (!timers_.empty()) {
        reset_timerfd(timers_.begin()->first);
    }
}

void TimerQueue::add_timer_in_loop(Timer* timer) {
    loop_->assert_in_loop_thread();

    if (pending_cancel_timers_.erase(timer)) {
        delete timer;
        return;
    }

    insert(timer);
}

void TimerQueue::cancel_in_loop(TimerId timer_id) {
    loop_->assert_in_loop_thread();

    Timer* timer = timer_id.timer_;
    if (!timer || timer->sequence() != timer_id.sequence_) {
        return;
    }

    TimerEntry entry(timer->expiration(), timer);
    auto it = timers_.find(entry);
    if (it == timers_.end()) {
        if (calling_expired_timers_) {
            canceling_timers_.insert(timer);
        } else {
            pending_cancel_timers_.insert(timer);
        }
        return;
    }

    const bool earliest = (it == timers_.begin());
    timers_.erase(it);

    if (calling_expired_timers_) {
        canceling_timers_.insert(timer);
    } else {
        delete timer;
        if (earliest && !timers_.empty()) {
            reset_timerfd(timers_.begin()->first);
        }
    }
}

void TimerQueue::insert(Timer* timer) {
    bool earliest_changed = timers_.empty() || timer->expiration() < timers_.begin()->first;
    timers_.emplace(timer->expiration(), timer);

    if (earliest_changed) {
        reset_timerfd(timer->expiration());
    }
}

std::vector<Timer*> TimerQueue::get_expired(Timestamp when) {
    std::vector<Timer*> expired;

    // sentry 的 second 取最大指针，lower_bound 得到所有 expiration <= when 的条目
    TimerEntry sentry(when, reinterpret_cast<Timer*>(UINTPTR_MAX));
    auto end = timers_.lower_bound(sentry);
    for (auto it = timers_.begin(); it != end;) {
        expired.push_back(it->second);
        it = timers_.erase(it);
    }

    return expired;
}

void TimerQueue::reset_timerfd(Timestamp expiration) {
    struct itimerspec spec{};
    spec.it_value = time_from_now(expiration);
    spec.it_interval = duration_to_spec(0);

    if (::timerfd_settime(timerfd_, 0, &spec, nullptr) < 0) {
        SNLOG_ERROR("TimerQueue: timerfd_settime failed, errno={}", errno);
    }
}

} // namespace solar_net
