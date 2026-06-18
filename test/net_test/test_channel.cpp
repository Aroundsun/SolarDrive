#include <gtest/gtest.h>

#include <atomic>
#include <memory>

#include <fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "channel.h"
#include "event_loop_test_util.h"

using solar_net::Channel;
using solar_net::EventLoop;
using solar_net::test::run_in_event_loop_thread;

namespace {

int create_pipe_read_end(int pipefd[2]) {
    if (::pipe(pipefd) != 0) {
        return -1;
    }
    return pipefd[0];
}

} // namespace

TEST(ChannelTest, HandleReadCallback) {
    run_in_event_loop_thread([](EventLoop& loop) {
        int pipefd[2];
        ASSERT_GE(create_pipe_read_end(pipefd), 0);

        Channel channel(&loop, pipefd[0]);
        bool called = false;
        channel.set_read_callback([&]() { called = true; });
        channel.set_revents(EPOLLIN);
        channel.handle_event();

        EXPECT_TRUE(called);
        ::close(pipefd[0]);
        ::close(pipefd[1]);
    });
}

TEST(ChannelTest, HandleWriteCallback) {
    run_in_event_loop_thread([](EventLoop& loop) {
        int pipefd[2];
        ASSERT_GE(create_pipe_read_end(pipefd), 0);

        Channel channel(&loop, pipefd[1]);
        bool called = false;
        channel.set_write_callback([&]() { called = true; });
        channel.set_revents(EPOLLOUT);
        channel.handle_event();

        EXPECT_TRUE(called);
        ::close(pipefd[0]);
        ::close(pipefd[1]);
    });
}

TEST(ChannelTest, HandleErrorCallback) {
    run_in_event_loop_thread([](EventLoop& loop) {
        int pipefd[2];
        ASSERT_GE(create_pipe_read_end(pipefd), 0);

        Channel channel(&loop, pipefd[0]);
        bool called = false;
        channel.set_error_callback([&]() { called = true; });
        channel.set_revents(EPOLLERR);
        channel.handle_event();

        EXPECT_TRUE(called);
        ::close(pipefd[0]);
        ::close(pipefd[1]);
    });
}

TEST(ChannelTest, HandleCloseCallbackWithoutRead) {
    run_in_event_loop_thread([](EventLoop& loop) {
        int pipefd[2];
        ASSERT_GE(create_pipe_read_end(pipefd), 0);

        Channel channel(&loop, pipefd[0]);
        bool called = false;
        channel.set_close_callback([&]() { called = true; });
        channel.set_revents(EPOLLHUP);
        channel.handle_event();

        EXPECT_TRUE(called);
        ::close(pipefd[0]);
        ::close(pipefd[1]);
    });
}

TEST(ChannelTest, HupWithReadTriggersReadNotClose) {
    run_in_event_loop_thread([](EventLoop& loop) {
        int pipefd[2];
        ASSERT_GE(create_pipe_read_end(pipefd), 0);

        Channel channel(&loop, pipefd[0]);
        bool read_called = false;
        bool close_called = false;
        channel.set_read_callback([&]() { read_called = true; });
        channel.set_close_callback([&]() { close_called = true; });
        channel.set_revents(EPOLLHUP | EPOLLIN);
        channel.handle_event();

        EXPECT_TRUE(read_called);
        EXPECT_FALSE(close_called);
        ::close(pipefd[0]);
        ::close(pipefd[1]);
    });
}

TEST(ChannelTest, TieSkipsCallbackWhenOwnerDestroyed) {
    run_in_event_loop_thread([](EventLoop& loop) {
        int pipefd[2];
        ASSERT_GE(create_pipe_read_end(pipefd), 0);

        Channel channel(&loop, pipefd[0]);
        bool called = false;
        channel.set_read_callback([&]() { called = true; });

        {
            auto owner = std::make_shared<int>(42);
            channel.tie(owner);
        }

        channel.set_revents(EPOLLIN);
        channel.handle_event();

        EXPECT_FALSE(called);
        ::close(pipefd[0]);
        ::close(pipefd[1]);
    });
}

TEST(ChannelTest, EnableAndDisableEvents) {
    run_in_event_loop_thread([](EventLoop& loop) {
        int pipefd[2];
        ASSERT_GE(create_pipe_read_end(pipefd), 0);

        Channel channel(&loop, pipefd[0]);
        EXPECT_TRUE(channel.is_none_event());

        channel.enable_reading();
        EXPECT_NE(channel.events() & EPOLLIN, 0);

        channel.enable_writing();
        EXPECT_NE(channel.events() & EPOLLOUT, 0);

        channel.disable_writing();
        EXPECT_EQ(channel.events() & EPOLLOUT, 0);

        channel.disable_all();
        EXPECT_TRUE(channel.is_none_event());

        ::close(pipefd[0]);
        ::close(pipefd[1]);
    });
}
