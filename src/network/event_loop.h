// =============================================================================
// event_loop.h — SolarDrive 网络层：Reactor 事件循环
//
// 模块职责：
//   - 单线程 Reactor 核心，每个 IO 线程持有一个 EventLoop
//   - 聚合 EpollPoller（I/O 多路复用）、TimerQueue（定时器）、eventfd（跨线程唤醒）
//   - 提供 run_in_loop / queue_in_loop，实现「在 IO 线程执行回调」的线程安全投递
//
// Reactor 模式要点：
//   poll → 遍历 active_channels_ 调用 handle_event → do_pending_tasks
//   跨线程 queue_in_loop 时通过 eventfd 唤醒阻塞在 epoll_wait 上的 loop 线程
// =============================================================================
#pragma once

#include "timer.h"

#include <functional>
#include <vector>
#include <mutex>
#include <atomic>
#include <memory>
#include <thread>

namespace solar_net {

class Channel;
class EpollPoller;
class TimerQueue;

/// Reactor 事件循环：单线程驱动 epoll，分发 I/O 与定时器事件。
/// 每个 IO 线程至多持有一个实例（thread_local 保证）；非线程安全，除 stop/run_in_loop 等标注项外须在 loop 线程访问。
class EventLoop {
public:
    using Task = std::function<void()>;

    static constexpr int kPollTimeoutMs = 10000;

    EventLoop();

    ~EventLoop();

    /// 运行事件循环。阻塞直到 stop() 被调用。
    void loop();

    /// 停止事件循环 (线程安全).
    void stop();

    /// 在当前线程运行一个任务。
    /// 如果当前线程是事件循环线程，则立即运行。
    /// 否则，将任务添加到任务队列中，稍后执行。
    void run_in_loop(Task task);

    /// 将一个任务添加到事件循环的线程中执行。
    /// 总是添加 — 即使从事件循环线程调用。
    void queue_in_loop(Task task);

    /// 断言调用线程是事件循环线程。
    void assert_in_loop_thread();

    /// 检查调用线程是否是事件循环线程。
    bool is_in_loop_thread() const;

    /// 更新通道的兴趣事件。
    void update_channel(Channel* channel);

    /// 从 poller 中移除一个通道。
    void remove_channel(Channel* channel);

    /// 获取当前线程的事件循环 (线程本地).
    static EventLoop* get_event_loop_of_current_thread();

    /// delay 秒后执行一次 callback.
    TimerId run_after(double delay, TimerCallback cb);

    /// 每 interval 秒执行一次 callback.
    TimerId run_every(double interval, TimerCallback cb);

    /// 取消定时器.
    void cancel(TimerId timer_id);

    // 禁用拷贝构造
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

private:
    /// 处理所有待处理的任务。
    void do_pending_tasks();

    /// 唤醒事件循环 (写入 eventfd).
    void wakeup();

    /// 处理 eventfd 读回调。
    void handle_read();

    /// 关闭 eventfd.
    void close_wakeup_fd();

    std::atomic<bool> looping_; // 是否正在循环
    std::atomic<bool> stop_; // 是否停止

    const std::thread::id thread_id_; // 线程 ID

    std::unique_ptr<EpollPoller> poller_; // poller
    std::unique_ptr<TimerQueue> timer_queue_;

    // 唤醒事件循环的文件描述符
    int wakeup_fd_; // eventfd 文件描述符
    std::unique_ptr<Channel> wakeup_channel_; // 唤醒事件循环的通道

    /// 待处理的任务队列
    std::vector<Task> pending_tasks_;
    std::mutex mutex_; // 互斥锁

    /// 从 poll 返回的活动通道
    std::vector<Channel*> active_channels_; // 活动通道
};

} // namespace solar_net
