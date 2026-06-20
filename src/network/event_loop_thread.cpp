// =============================================================================
// event_loop_thread.cpp — IO 线程启动与 EventLoop 生命周期
// =============================================================================
#include "event_loop_thread.h"
#include "event_loop.h"

namespace solar_net {

EventLoopThread::EventLoopThread(const ThreadInitCallback& cb)
    : loop_(nullptr)
    , callback_(cb) {}

EventLoopThread::~EventLoopThread() {
    if (loop_ != nullptr) {
        loop_->stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }
}

EventLoop* EventLoopThread::start_loop() {
    thread_ = std::thread(&EventLoopThread::thread_func, this);

    // 条件变量等待 loop_ 在 thread_func 中完成赋值，保证 start_loop 返回可用指针
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this]() { return loop_ != nullptr; });
    }

    return loop_;
}

void EventLoopThread::thread_func() {
    EventLoop loop;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        loop_ = &loop;
        cv_.notify_one();
    }

    if (callback_) {
        callback_(loop_);
    }

    loop.loop();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        loop_ = nullptr;
    }
}

} // namespace solar_net
