// =============================================================================
// event_loop_thread.h — SolarDrive 网络层：单 IO 线程
//
// 模块职责：
//   - 在独立 std::thread 中创建并运行 EventLoop
//   - start_loop() 阻塞直到 loop 就绪，供 TcpServer 线程池收集 EventLoop 指针
// =============================================================================
#pragma once

#include <functional>
#include <mutex>
#include <condition_variable>
#include <thread>

namespace solar_net {

class EventLoop;

/// 单 IO 线程：thread 内构造 EventLoop 并 loop()，对外暴露 loop 指针。
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
