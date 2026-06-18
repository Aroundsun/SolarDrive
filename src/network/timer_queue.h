#pragma once

#include <memory>
#include <set>
#include <vector>

#include "timer.h"

namespace solar_net {

class Channel;
class EventLoop;
// 定时器队列，用于管理定时器

class TimerQueue {
public:
    using TimerEntry = std::pair<Timestamp, Timer*>;

    explicit TimerQueue(EventLoop* loop);
    ~TimerQueue();

    /// 添加定时器
    TimerId add_timer(TimerCallback cb, Timestamp when, double interval);

    /// 取消定时器
    void cancel(TimerId timer_id);

    /// 获取下一个定时器的超时时间
    int get_next_timeout_ms() const;

private:
    /// 处理 timerfd 读事件
    void handle_read();

    /// 在事件循环中添加定时器
    void add_timer_in_loop(Timer* timer);

    /// 在事件循环中取消定时器
    /// 如果定时器正在调用过期的定时器，则将其添加到取消的定时器集合中
    void cancel_in_loop(TimerId timer_id);

    /// 插入定时器
    /// 如果定时器是最早的定时器，则重置 timerfd 的超时时间
    void insert(Timer* timer);

    /// 获取过期的定时器
    /// 返回过期的定时器集合
    std::vector<Timer*> get_expired(Timestamp now);

    /// 重置 timerfd 的超时时间
    void reset_timerfd(Timestamp expiration);

    EventLoop* loop_; // 所属的事件循环
    const int timerfd_; // timerfd 文件描述符
    std::unique_ptr<Channel> timerfd_channel_; // timerfd 通道

    std::set<TimerEntry> timers_; // 定时器集合
    bool calling_expired_timers_ = false; // 是否正在调用过期的定时器
    std::set<Timer*> canceling_timers_; // 过期回调期间取消的定时器
    std::set<Timer*> pending_cancel_timers_; // 尚未入队即被取消的定时器
};

} // namespace solar_net
