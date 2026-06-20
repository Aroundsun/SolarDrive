// =============================================================================
// event_loop_thread_pool.cpp — 线程池启动与 round-robin 选 loop
// =============================================================================
#include "event_loop_thread_pool.h"
#include "event_loop.h"
#include "event_loop_thread.h"
#include "log.h"

#include <cassert>

namespace solar_net {

EventLoopThreadPool::EventLoopThreadPool(EventLoop* base_loop)
    : base_loop_(base_loop) {}

EventLoopThreadPool::~EventLoopThreadPool() = default;

void EventLoopThreadPool::set_thread_num(int num_threads) {
    num_threads_ = num_threads;
}

void EventLoopThreadPool::start(const ThreadInitCallback& cb) {
    assert(!started_);
    base_loop_->assert_in_loop_thread();

    started_ = true;

    for (int i = 0; i < num_threads_; ++i) {
        auto thread_ptr = std::make_unique<EventLoopThread>(cb);
        EventLoop* loop = thread_ptr->start_loop();
        loops_.push_back(loop);
        threads_.push_back(std::move(thread_ptr));
    }

    if (num_threads_ == 0 && cb) {
        cb(base_loop_);
    }

    SNLOG_INFO("EventLoopThreadPool: started with {} IO thread(s)", num_threads_);
}

EventLoop* EventLoopThreadPool::get_next_loop() {
    base_loop_->assert_in_loop_thread();

    if (loops_.empty()) {
        // 单线程模式：无 IO 子线程，accept 与连接读写均在 base_loop
        return base_loop_;
    }

    EventLoop* loop = loops_[next_index_];
    next_index_ = (next_index_ + 1) % loops_.size();
    return loop;
}

EventLoop* EventLoopThreadPool::get_loop(std::size_t index) const {
    base_loop_->assert_in_loop_thread();

    if (index < loops_.size()) {
        return loops_[index];
    }
    return base_loop_;
}

const std::vector<EventLoop*>& EventLoopThreadPool::get_all_loops() const {
    base_loop_->assert_in_loop_thread();

    if (loops_.empty()) {
        static thread_local std::vector<EventLoop*> base_only;
        base_only.clear();
        base_only.push_back(base_loop_);
        return base_only;
    }
    return loops_;
}

} // namespace solar_net
