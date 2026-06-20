// =============================================================================
// event_loop_thread_pool.h — SolarDrive 网络层：IO EventLoop 线程池
//
// 模块职责：
//   - 创建 num_threads_ 个 EventLoopThread，收集各 IO EventLoop
//   - get_next_loop() round-robin 分配新连接；num_threads_=0 时退化为单线程（base_loop）
// =============================================================================
#pragma once

#include <vector>
#include <memory>
#include <functional>

namespace solar_net {

class EventLoop;
class EventLoopThread;

/// IO 线程池：多 Reactor 实例，主 loop 负责 accept，连接 round-robin 到 IO loop。
class EventLoopThreadPool {
public:
    using ThreadInitCallback = std::function<void(EventLoop*)>;

    /// 构造一个 EventLoopThreadPool。
    /// @param base_loop 主 EventLoop (acceptor)。
    explicit EventLoopThreadPool(EventLoop* base_loop);

    ~EventLoopThreadPool();

    /// 设置 IO 线程的数量。必须在 start() 之前调用。
    void set_thread_num(int num_threads);

    /// 启动线程池。
    /// @param cb 当线程循环启动时调用的回调。
    void start(const ThreadInitCallback& cb = ThreadInitCallback());

    /// 获取下一个 EventLoop (循环调度)。
    /// 如果池为空 (单线程模式)，返回主 EventLoop。
    EventLoop* get_next_loop();

    /// 获取一个 EventLoop 通过索引。
    EventLoop* get_loop(std::size_t index) const;

    /// 获取所有 IO 循环。
    const std::vector<EventLoop*>& get_all_loops() const;

    /// 获取主 EventLoop。
    EventLoop* base_loop() const { return base_loop_; }

    /// 检查池是否已启动。
    bool started() const { return started_; }

    /// 获取 IO 线程的数量。
    std::size_t thread_num() const { return threads_.size(); }

private:
    EventLoop* base_loop_; // 主 EventLoop
    bool started_ = false; // 是否已启动
    int num_threads_ = 0; // IO 线程的数量
    std::size_t next_index_ = 0; // 下一个索引

    std::vector<std::unique_ptr<EventLoopThread>> threads_; // IO 线程
    std::vector<EventLoop*> loops_; // IO 循环
};

} // namespace solar_net
