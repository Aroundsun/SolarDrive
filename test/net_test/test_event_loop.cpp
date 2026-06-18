#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

#include <unistd.h>

#include "event_loop.h"
#include "channel.h"
#include "event_loop_test_util.h"

using solar_net::Channel;
using solar_net::EventLoop;
using solar_net::test::run_in_event_loop_thread;

using namespace std::chrono_literals;

TEST(EventLoopTest, IsInLoopThread) {
    run_in_event_loop_thread([](EventLoop& loop) {
        EXPECT_TRUE(loop.is_in_loop_thread());
    });
}

TEST(EventLoopTest, GetEventLoopOfCurrentThread) {
    run_in_event_loop_thread([](EventLoop& loop) {
        EXPECT_EQ(EventLoop::get_event_loop_of_current_thread(), &loop);
    });
}

TEST(EventLoopTest, RunInLoopExecutesImmediatelyOnLoopThread) {
    run_in_event_loop_thread([](EventLoop& loop) {
        bool called = false;
        loop.run_in_loop([&]() { called = true; });
        EXPECT_TRUE(called);
    });
}

TEST(EventLoopTest, QueueInLoopFromOtherThread) {
    std::promise<void> task_done;
    auto task_future = task_done.get_future();

    run_in_event_loop_thread([&](EventLoop& loop) {
        std::thread worker([&]() {
            loop.queue_in_loop([&]() {
                task_done.set_value();
                loop.stop();
            });
        });
        worker.detach();
        loop.loop();
    });

    EXPECT_EQ(task_future.wait_for(2s), std::future_status::ready);
}

TEST(EventLoopTest, CrossThreadTaskRunsOnLoopThread) {
    std::promise<std::thread::id> tid_promise;
    auto tid_future = tid_promise.get_future();

    run_in_event_loop_thread([&](EventLoop& loop) {
        const auto loop_tid = std::this_thread::get_id();

        std::thread worker([&]() {
            loop.run_in_loop([&]() {
                tid_promise.set_value(std::this_thread::get_id());
                loop.stop();
            });
        });
        worker.detach();
        loop.loop();

        EXPECT_EQ(tid_future.wait_for(2s), std::future_status::ready);
        EXPECT_EQ(tid_future.get(), loop_tid);
    });
}

TEST(EventLoopTest, StopFromAnotherThread) {
    std::atomic<bool> loop_exited{false};

    std::thread loop_thread([&]() {
        EventLoop loop;
        std::thread stopper([&]() {
            std::this_thread::sleep_for(50ms);
            loop.stop();
        });

        loop.loop();
        stopper.join();
        loop_exited = true;
    });

    loop_thread.join();
    EXPECT_TRUE(loop_exited);
}

TEST(EventLoopTest, PollTriggersReadOnPipe) {
    std::atomic<bool> read_done{false};

    run_in_event_loop_thread([&](EventLoop& loop) {
        int pipefd[2];
        ASSERT_EQ(::pipe(pipefd), 0);

        Channel channel(&loop, pipefd[0]);
        channel.set_read_callback([&]() {
            char buf[8] = {};
            ::read(pipefd[0], buf, sizeof(buf));
            read_done = true;
            loop.stop();
        });
        channel.enable_reading();

        std::thread writer([&]() {
            const char msg = 'x';
            ::write(pipefd[1], &msg, 1);
            ::close(pipefd[1]);
        });
        writer.detach();

        loop.loop();

        ::close(pipefd[0]);
    });

    EXPECT_TRUE(read_done);
}

TEST(EventLoopTest, PendingTasksRunAfterPoll) {
    std::atomic<int> counter{0};

    run_in_event_loop_thread([&](EventLoop& loop) {
        loop.queue_in_loop([&]() { counter.fetch_add(1); });
        loop.queue_in_loop([&]() {
            counter.fetch_add(1);
            loop.stop();
        });

        std::thread waker([&]() { loop.stop(); });
        waker.detach();

        loop.loop();
    });

    EXPECT_EQ(counter.load(), 2);
}
