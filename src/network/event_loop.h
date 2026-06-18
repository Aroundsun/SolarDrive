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

// 核心事件循环。每个线程一个。
// 通过 EpollPoller 驱动 I/O 多路复用，并支持跨线程任务调度
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
