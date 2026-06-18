#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <string>
#include <thread>

#include <unistd.h>

#include "event_loop.h"
#include "tcp_connection.h"
#include "event_loop_test_util.h"
#include "tcp_test_util.h"

using solar_net::EventLoop;
using solar_net::TcpConnection;
using solar_net::TcpConnectionPtr;
using solar_net::test::close_tcp_socket_pair;
using solar_net::test::make_tcp_socket_pair;
using solar_net::test::run_in_event_loop_thread;
using solar_net::test::stop_loop_after;

using namespace std::chrono_literals;

TEST(TcpConnectionTest, ConnectionEstablishedSetsStateAndFiresCallback) {
    run_in_event_loop_thread([](EventLoop& loop) {
        auto pair = make_tcp_socket_pair();
        ASSERT_GE(pair.server_fd, 0);

        bool callback_fired = false;
        auto conn = std::make_shared<TcpConnection>(
            &loop, "conn-1", pair.server_fd, pair.server_local, pair.server_peer);
        conn->set_connection_callback([&](const TcpConnectionPtr& c) {
            callback_fired = true;
            EXPECT_EQ(c->name(), "conn-1");
        });

        conn->connection_established();
        EXPECT_EQ(conn->state(), TcpConnection::State::kConnected);
        EXPECT_TRUE(callback_fired);

        conn->connection_destroyed();
        close_tcp_socket_pair(pair);
    });
}

TEST(TcpConnectionTest, ReceivesMessageFromPeer) {
    std::atomic<bool> received{false};

    run_in_event_loop_thread([&](EventLoop& loop) {
        auto pair = make_tcp_socket_pair();
        ASSERT_GE(pair.server_fd, 0);

        auto conn = std::make_shared<TcpConnection>(
            &loop, "recv", pair.server_fd, pair.server_local, pair.server_peer);
        conn->set_message_callback([&](const TcpConnectionPtr&, solar_net::Buffer* buf, int64_t) {
            EXPECT_EQ(buf->retrieve_all_as_string(), "hello");
            received = true;
            loop.stop();
        });

        conn->connection_established();
        ASSERT_EQ(::write(pair.client_fd, "hello", 5), 5);

        loop.loop();

        conn->connection_destroyed();
        close_tcp_socket_pair(pair);
    });

    EXPECT_TRUE(received);
}

TEST(TcpConnectionTest, SendsMessageToPeer) {
    run_in_event_loop_thread([](EventLoop& loop) {
        auto pair = make_tcp_socket_pair();
        ASSERT_GE(pair.server_fd, 0);

        auto conn = std::make_shared<TcpConnection>(
            &loop, "send", pair.server_fd, pair.server_local, pair.server_peer);
        conn->connection_established();
        conn->send("reply");

        char buf[16] = {};
        const ssize_t n = ::read(pair.client_fd, buf, sizeof(buf));
        ASSERT_GT(n, 0);
        EXPECT_EQ(std::string(buf, static_cast<std::size_t>(n)), "reply");

        conn->connection_destroyed();
        close_tcp_socket_pair(pair);
    });
}

TEST(TcpConnectionTest, CrossThreadSendDeliveredToPeer) {
    std::promise<void> sent;
    auto sent_future = sent.get_future();

    run_in_event_loop_thread([&](EventLoop& loop) {
        auto pair = make_tcp_socket_pair();
        ASSERT_GE(pair.server_fd, 0);

        auto conn = std::make_shared<TcpConnection>(
            &loop, "cross", pair.server_fd, pair.server_local, pair.server_peer);
        conn->connection_established();

        std::thread worker([&]() {
            conn->send("from-worker");
            sent.set_value();
        });

        stop_loop_after(loop, 200ms);
        loop.loop();
        worker.join();

        EXPECT_EQ(sent_future.wait_for(0s), std::future_status::ready);

        char buf[32] = {};
        const ssize_t n = ::read(pair.client_fd, buf, sizeof(buf));
        ASSERT_GT(n, 0);
        EXPECT_EQ(std::string(buf, static_cast<std::size_t>(n)), "from-worker");

        conn->connection_destroyed();
        close_tcp_socket_pair(pair);
    });
}

TEST(TcpConnectionTest, WriteCompleteCallbackFires) {
    std::atomic<bool> write_done{false};

    run_in_event_loop_thread([&](EventLoop& loop) {
        auto pair = make_tcp_socket_pair();
        ASSERT_GE(pair.server_fd, 0);

        auto conn = std::make_shared<TcpConnection>(
            &loop, "write-cb", pair.server_fd, pair.server_local, pair.server_peer);
        conn->set_write_complete_callback([&](const TcpConnectionPtr&) {
            write_done = true;
        });

        conn->connection_established();
        conn->send("done");

        std::thread drainer([&]() {
            char buf[16] = {};
            ::read(pair.client_fd, buf, sizeof(buf));
        });
        drainer.detach();

        stop_loop_after(loop, 500ms);
        loop.loop();

        conn->connection_destroyed();
        close_tcp_socket_pair(pair);
    });

    EXPECT_TRUE(write_done);
}

TEST(TcpConnectionTest, PeerCloseTriggersCloseCallback) {
    std::atomic<bool> close_called{false};

    run_in_event_loop_thread([&](EventLoop& loop) {
        auto pair = make_tcp_socket_pair();
        ASSERT_GE(pair.server_fd, 0);

        auto conn = std::make_shared<TcpConnection>(
            &loop, "peer-close", pair.server_fd, pair.server_local, pair.server_peer);
        conn->set_close_callback([&](const TcpConnectionPtr& c) {
            close_called = true;
            EXPECT_EQ(c->state(), TcpConnection::State::kDisconnected);
            loop.stop();
        });

        conn->connection_established();
        ::close(pair.client_fd);
        pair.client_fd = -1;

        loop.loop();

        conn->connection_destroyed();
        close_tcp_socket_pair(pair);
    });

    EXPECT_TRUE(close_called);
}

TEST(TcpConnectionTest, ForceCloseDisconnects) {
    std::atomic<bool> close_called{false};

    run_in_event_loop_thread([&](EventLoop& loop) {
        auto pair = make_tcp_socket_pair();
        ASSERT_GE(pair.server_fd, 0);

        auto conn = std::make_shared<TcpConnection>(
            &loop, "force", pair.server_fd, pair.server_local, pair.server_peer);
        conn->set_close_callback([&](const TcpConnectionPtr&) {
            close_called = true;
            loop.stop();
        });

        conn->connection_established();

        std::thread worker([&]() {
            std::this_thread::sleep_for(20ms);
            conn->force_close();
        });
        worker.detach();

        loop.loop();

        EXPECT_EQ(conn->state(), TcpConnection::State::kDisconnected);
        conn->connection_destroyed();
        close_tcp_socket_pair(pair);
    });

    EXPECT_TRUE(close_called);
}

TEST(TcpConnectionTest, SendBufferConsumesReadableBytes) {
    run_in_event_loop_thread([](EventLoop& loop) {
        auto pair = make_tcp_socket_pair();
        ASSERT_GE(pair.server_fd, 0);

        auto conn = std::make_shared<TcpConnection>(
            &loop, "buf-send", pair.server_fd, pair.server_local, pair.server_peer);
        conn->connection_established();

        solar_net::Buffer buf;
        buf.append("buffered");
        conn->send(&buf);
        EXPECT_EQ(buf.readable_bytes(), 0u);

        char data[16] = {};
        const ssize_t n = ::read(pair.client_fd, data, sizeof(data));
        ASSERT_GT(n, 0);
        EXPECT_EQ(std::string(data, static_cast<std::size_t>(n)), "buffered");

        conn->connection_destroyed();
        close_tcp_socket_pair(pair);
    });
}
