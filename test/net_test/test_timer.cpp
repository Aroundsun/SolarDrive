#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

#include "event_loop.h"
#include "event_loop_test_util.h"

using solar_net::EventLoop;
using solar_net::TimerId;
using solar_net::test::run_in_event_loop_thread;

using namespace std::chrono_literals;

TEST(TimerTest, RunAfterFiresOnce) {
    std::atomic<int> count{0};

    run_in_event_loop_thread([&](EventLoop& loop) {
        loop.run_after(0.05, [&]() {
            ++count;
            loop.stop();
        });
        loop.loop();
    });

    EXPECT_EQ(count.load(), 1);
}

TEST(TimerTest, RunEveryFiresMultipleTimes) {
    std::atomic<int> count{0};

    run_in_event_loop_thread([&](EventLoop& loop) {
        loop.run_every(0.03, [&]() {
            if (++count >= 3) {
                loop.stop();
            }
        });
        loop.loop();
    });

    EXPECT_GE(count.load(), 3);
}

TEST(TimerTest, CancelPreventsFire) {
    std::atomic<int> count{0};

    run_in_event_loop_thread([&](EventLoop& loop) {
        TimerId id = loop.run_after(0.05, [&]() { ++count; });
        loop.cancel(id);

        loop.run_after(0.08, [&]() { loop.stop(); });
        loop.loop();
    });

    EXPECT_EQ(count.load(), 0);
}

TEST(TimerTest, CancelBeforeAddInLoop) {
    std::atomic<int> count{0};

    run_in_event_loop_thread([&](EventLoop& loop) {
        std::thread worker([&]() {
            TimerId id = loop.run_after(0.05, [&]() { ++count; });
            loop.cancel(id);
        });
        worker.join();

        loop.run_after(0.1, [&]() { loop.stop(); });
        loop.loop();
    });

    EXPECT_EQ(count.load(), 0);
}

TEST(TimerTest, AddTimerFromOtherThread) {
    std::promise<void> fired;
    auto fired_future = fired.get_future();

    run_in_event_loop_thread([&](EventLoop& loop) {
        std::thread worker([&]() {
            loop.run_after(0.05, [&]() {
                fired.set_value();
                loop.stop();
            });
        });
        worker.detach();
        loop.loop();
    });

    EXPECT_EQ(fired_future.wait_for(2s), std::future_status::ready);
}
