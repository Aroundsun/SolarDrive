#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "acceptor.h"
#include "event_loop.h"
#include "event_loop_test_util.h"

using solar_net::Acceptor;
using solar_net::EventLoop;
using solar_net::test::run_in_event_loop_thread;
using solar_net::test::stop_loop_after;

using namespace std::chrono_literals;

namespace {

int connect_to(uint16_t port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    ::sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    if (::connect(fd, reinterpret_cast<::sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return -1;
    }

    return fd;
}

::sockaddr_in make_loopback_addr(uint16_t port = 0) {
    ::sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    return addr;
}

} // namespace

TEST(AcceptorTest, PortReturnsBoundPort) {
    run_in_event_loop_thread([](EventLoop& loop) {
        Acceptor acceptor(&loop, make_loopback_addr(0));
        EXPECT_GT(acceptor.port(), 0u);
    });
}

TEST(AcceptorTest, ListenSetsListeningFlag) {
    run_in_event_loop_thread([](EventLoop& loop) {
        Acceptor acceptor(&loop, make_loopback_addr(0));
        EXPECT_FALSE(acceptor.listening());

        acceptor.listen();
        EXPECT_TRUE(acceptor.listening());
    });
}

TEST(AcceptorTest, AcceptsIncomingConnection) {
    std::atomic<int> accepted_fd{-1};

    run_in_event_loop_thread([&](EventLoop& loop) {
        Acceptor acceptor(&loop, make_loopback_addr(0));
        const uint16_t port = acceptor.port();

        acceptor.set_new_connection_callback([&](int fd, const ::sockaddr_in& peer) {
            accepted_fd = fd;
            EXPECT_EQ(peer.sin_addr.s_addr, htonl(INADDR_LOOPBACK));
            loop.stop();
        });
        acceptor.listen();

        std::thread client([port]() {
            const int fd = connect_to(port);
            if (fd >= 0) {
                ::close(fd);
            }
        });

        stop_loop_after(loop, 500ms);
        loop.loop();
        client.join();

        if (accepted_fd >= 0) {
            ::close(accepted_fd);
        }
    });

    EXPECT_GE(accepted_fd.load(), 0);
}

TEST(AcceptorTest, AcceptsMultipleConnections) {
    std::atomic<int> accept_count{0};
    std::vector<int> accepted_fds;

    run_in_event_loop_thread([&](EventLoop& loop) {
        Acceptor acceptor(&loop, make_loopback_addr(0));
        const uint16_t port = acceptor.port();

        acceptor.set_new_connection_callback([&](int fd, const ::sockaddr_in&) {
            accepted_fds.push_back(fd);
            if (accept_count.fetch_add(1) + 1 >= 2) {
                loop.stop();
            }
        });
        acceptor.listen();

        std::thread clients([port]() {
            for (int i = 0; i < 2; ++i) {
                const int fd = connect_to(port);
                if (fd >= 0) {
                    ::close(fd);
                }
                std::this_thread::sleep_for(10ms);
            }
        });

        stop_loop_after(loop, 500ms);
        loop.loop();
        clients.join();

        for (int fd : accepted_fds) {
            ::close(fd);
        }
    });

    EXPECT_GE(accept_count.load(), 2);
}

TEST(AcceptorTest, AcceptedSocketIsNonBlocking) {
    std::atomic<int> accepted_fd{-1};

    run_in_event_loop_thread([&](EventLoop& loop) {
        Acceptor acceptor(&loop, make_loopback_addr(0));
        const uint16_t port = acceptor.port();

        acceptor.set_new_connection_callback([&](int fd, const ::sockaddr_in&) {
            accepted_fd = fd;
            loop.stop();
        });
        acceptor.listen();

        std::thread client([port]() {
            const int fd = connect_to(port);
            if (fd >= 0) {
                ::close(fd);
            }
        });

        stop_loop_after(loop, 500ms);
        loop.loop();
        client.join();

        if (accepted_fd >= 0) {
            const int flags = fcntl(accepted_fd, F_GETFL, 0);
            EXPECT_NE(flags & O_NONBLOCK, 0);
            ::close(accepted_fd);
        }
    });

    EXPECT_GE(accepted_fd.load(), 0);
}

TEST(AcceptorTest, ClosesConnectionWhenNoCallback) {
    run_in_event_loop_thread([&](EventLoop& loop) {
        Acceptor acceptor(&loop, make_loopback_addr(0));
        const uint16_t port = acceptor.port();
        acceptor.listen();

        const int client_fd = connect_to(port);
        ASSERT_GE(client_fd, 0);

        stop_loop_after(loop, 100ms);
        loop.loop();

        ::close(client_fd);
    });
}
