#pragma once

#include <functional>
#include <mutex>
#include <condition_variable>
#include <thread>

namespace solar_net {

class EventLoop;

/// 一个 IO 线程，拥有自己的 EventLoop。
/// 在启动时调用线程初始化回调。
/// 在专用线程中启动事件循环，并提供对循环的访问以进行连接分发。
class EventLoopThread {
public:
    using ThreadInitCallback = std::function<void(EventLoop*)>;

    /// 构造一个 EventLoopThread.
    /// @param cb 当循环启动时调用的可选回调.
    explicit EventLoopThread(const ThreadInitCallback& cb = ThreadInitCallback());

    ~EventLoopThread();

    /// 启动 IO 线程并返回其 EventLoop。
    /// 阻塞直到循环初始化。
    EventLoop* start_loop();

private:
    /// 线程入口函数。
    void thread_func();

    EventLoop* loop_ = nullptr; // 事件循环
    std::thread thread_; // 线程
    ThreadInitCallback callback_; // 线程初始化回调

    std::mutex mutex_; // 互斥锁 用于保护 loop_ 的访问
    std::condition_variable cv_; // 条件变量 用于等待 loop_ 的初始化
};

} // namespace solar_net
