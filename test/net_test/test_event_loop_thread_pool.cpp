#include <gtest/gtest.h>

#include <atomic>
#include <set>
#include <vector>

#include "event_loop.h"
#include "event_loop_thread_pool.h"
#include "event_loop_test_util.h"

using solar_net::EventLoop;
using solar_net::EventLoopThreadPool;
using solar_net::test::run_in_event_loop_thread;

TEST(EventLoopThreadPoolTest, ZeroThreadsReturnsBaseLoop) {
    run_in_event_loop_thread([](EventLoop& base) {
        EventLoopThreadPool pool(&base);
        pool.set_thread_num(0);

        bool cb_called = false;
        pool.start([&](EventLoop* loop) {
            cb_called = true;
            EXPECT_EQ(loop, &base);
            EXPECT_TRUE(base.is_in_loop_thread());
        });

        EXPECT_TRUE(pool.started());
        EXPECT_EQ(pool.thread_num(), 0u);
        EXPECT_EQ(pool.get_next_loop(), &base);
        EXPECT_EQ(pool.get_loop(0), &base);
        EXPECT_TRUE(cb_called);

        const auto& loops = pool.get_all_loops();
        ASSERT_EQ(loops.size(), 1u);
        EXPECT_EQ(loops[0], &base);
    });
}

TEST(EventLoopThreadPoolTest, RoundRobinDispatch) {
    run_in_event_loop_thread([](EventLoop& base) {
        EventLoopThreadPool pool(&base);
        pool.set_thread_num(2);
        pool.start();

        EXPECT_EQ(pool.thread_num(), 2u);

        EventLoop* loop0 = pool.get_next_loop();
        EventLoop* loop1 = pool.get_next_loop();
        EventLoop* loop2 = pool.get_next_loop();

        EXPECT_NE(loop0, loop1);
        EXPECT_EQ(loop0, loop2);
        EXPECT_NE(loop0, &base);
        EXPECT_NE(loop1, &base);
    });
}

TEST(EventLoopThreadPoolTest, GetLoopByIndex) {
    run_in_event_loop_thread([](EventLoop& base) {
        EventLoopThreadPool pool(&base);
        pool.set_thread_num(2);
        pool.start();

        EventLoop* loop0 = pool.get_loop(0);
        EventLoop* loop1 = pool.get_loop(1);
        EventLoop* fallback = pool.get_loop(99);

        EXPECT_NE(loop0, loop1);
        EXPECT_EQ(fallback, &base);
    });
}

TEST(EventLoopThreadPoolTest, InitCallbackPerThread) {
    std::atomic<int> count{0};

    run_in_event_loop_thread([&](EventLoop& base) {
        EventLoopThreadPool pool(&base);
        pool.set_thread_num(2);
        pool.start([&](EventLoop* loop) {
            count.fetch_add(1);
            EXPECT_TRUE(loop->is_in_loop_thread());
        });
    });

    EXPECT_EQ(count.load(), 2);
}

TEST(EventLoopThreadPoolTest, GetAllLoopsContainsWorkerLoops) {
    run_in_event_loop_thread([](EventLoop& base) {
        EventLoopThreadPool pool(&base);
        pool.set_thread_num(2);
        pool.start();

        const auto& loops = pool.get_all_loops();
        ASSERT_EQ(loops.size(), 2u);

        std::set<EventLoop*> unique_loops(loops.begin(), loops.end());
        EXPECT_EQ(unique_loops.size(), 2u);
        EXPECT_EQ(unique_loops.count(&base), 0u);
    });
}

TEST(EventLoopThreadPoolTest, CrossThreadTaskOnWorkerLoop) {
    std::promise<void> task_done;
    auto task_future = task_done.get_future();

    run_in_event_loop_thread([&](EventLoop& base) {
        EventLoopThreadPool pool(&base);
        pool.set_thread_num(1);
        pool.start();

        EventLoop* worker = pool.get_next_loop();
        ASSERT_NE(worker, &base);

        worker->run_in_loop([&]() {
            EXPECT_TRUE(worker->is_in_loop_thread());
            task_done.set_value();
        });

        EXPECT_EQ(task_future.wait_for(std::chrono::seconds(2)),
                  std::future_status::ready);
    });
}
