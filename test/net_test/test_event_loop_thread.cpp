#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

#include "event_loop.h"
#include "event_loop_thread.h"

using solar_net::EventLoop;
using solar_net::EventLoopThread;

using namespace std::chrono_literals;

TEST(EventLoopThreadTest, StartLoopReturnsRunningLoop) {
    EventLoopThread io_thread;
    EventLoop* loop = io_thread.start_loop();
    ASSERT_NE(loop, nullptr);

    std::promise<void> done;
    auto done_future = done.get_future();
    loop->run_in_loop([&]() {
        EXPECT_TRUE(loop->is_in_loop_thread());
        done.set_value();
    });

    EXPECT_EQ(done_future.wait_for(2s), std::future_status::ready);
}

TEST(EventLoopThreadTest, InitCallbackRunsOnLoopThread) {
    std::promise<std::thread::id> init_tid;
    auto init_future = init_tid.get_future();

    EventLoopThread io_thread([&](EventLoop* loop) {
        init_tid.set_value(std::this_thread::get_id());
        EXPECT_TRUE(loop->is_in_loop_thread());
    });

    EventLoop* loop = io_thread.start_loop();
    ASSERT_NE(loop, nullptr);

    std::promise<std::thread::id> task_tid;
    auto task_future = task_tid.get_future();
    loop->run_in_loop([&]() { task_tid.set_value(std::this_thread::get_id()); });

    EXPECT_EQ(init_future.wait_for(2s), std::future_status::ready);
    EXPECT_EQ(task_future.wait_for(2s), std::future_status::ready);
    EXPECT_EQ(init_future.get(), task_future.get());
}

TEST(EventLoopThreadTest, CrossThreadRunInLoop) {
    EventLoopThread io_thread;
    EventLoop* loop = io_thread.start_loop();
    ASSERT_NE(loop, nullptr);

    std::promise<std::thread::id> tid_promise;
    auto tid_future = tid_promise.get_future();

    std::thread worker([&]() {
        loop->run_in_loop([&]() {
            tid_promise.set_value(std::this_thread::get_id());
        });
    });

    std::promise<std::thread::id> loop_tid_promise;
    auto loop_tid_future = loop_tid_promise.get_future();
    loop->run_in_loop([&]() { loop_tid_promise.set_value(std::this_thread::get_id()); });

    worker.join();

    EXPECT_EQ(tid_future.wait_for(2s), std::future_status::ready);
    EXPECT_EQ(loop_tid_future.wait_for(2s), std::future_status::ready);
    EXPECT_EQ(tid_future.get(), loop_tid_future.get());
}

TEST(EventLoopThreadTest, DestructorStopsLoop) {
    std::atomic<bool> loop_exited{false};

    {
        EventLoopThread io_thread;
        EventLoop* loop = io_thread.start_loop();
        loop->run_in_loop([&]() { loop_exited = true; });
        std::this_thread::sleep_for(50ms);
    }

    EXPECT_TRUE(loop_exited);
}
