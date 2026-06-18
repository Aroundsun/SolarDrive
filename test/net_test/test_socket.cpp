#include <gtest/gtest.h>

#include <cstring>
#include <memory>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "socket.h"

using solar_net::Socket;

namespace {

int create_tcp_socket() {
    return ::socket(AF_INET, SOCK_STREAM, 0);
}

} // namespace

TEST(SocketTest, FdAccessor) {
    const int fd = create_tcp_socket();
    ASSERT_GE(fd, 0);

    Socket sock(fd);
    EXPECT_EQ(sock.fd(), fd);
}

TEST(SocketTest, MoveConstruction) {
    const int fd = create_tcp_socket();
    ASSERT_GE(fd, 0);

    Socket a(fd);
    Socket b(std::move(a));

    EXPECT_EQ(b.fd(), fd);
    EXPECT_EQ(a.fd(), -1);
}

TEST(SocketTest, MoveAssignment) {
    const int fd1 = create_tcp_socket();
    const int fd2 = create_tcp_socket();
    ASSERT_GE(fd1, 0);
    ASSERT_GE(fd2, 0);

    Socket a(fd1);
    Socket b(fd2);
    b = std::move(a);

    EXPECT_EQ(b.fd(), fd1);
    EXPECT_EQ(a.fd(), -1);
}

TEST(SocketTest, SetNonBlocking) {
    const int fd = create_tcp_socket();
    ASSERT_GE(fd, 0);

    EXPECT_EQ(Socket::set_non_blocking(fd), 0);

    const int flags = fcntl(fd, F_GETFL, 0);
    EXPECT_NE(flags & O_NONBLOCK, 0);

    close(fd);
}

TEST(SocketTest, SetTcpOptions) {
    const int fd = create_tcp_socket();
    ASSERT_GE(fd, 0);

    EXPECT_EQ(Socket::set_keep_alive(fd), 0);
    EXPECT_EQ(Socket::set_tcp_no_delay(fd), 0);
    EXPECT_EQ(Socket::set_reuse_addr(fd), 0);
    EXPECT_EQ(Socket::set_reuse_port(fd), 0);

    close(fd);
}

TEST(SocketTest, GetLocalAndPeerAddr) {
    const int listen_fd = create_tcp_socket();
    ASSERT_GE(listen_fd, 0);
    ASSERT_EQ(Socket::set_reuse_addr(listen_fd), 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0);

    ASSERT_EQ(bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    ASSERT_EQ(listen(listen_fd, 1), 0);

    socklen_t len = sizeof(addr);
    ASSERT_EQ(getsockname(listen_fd, reinterpret_cast<sockaddr*>(&addr), &len), 0);

    const int client_fd = create_tcp_socket();
    ASSERT_GE(client_fd, 0);
    ASSERT_EQ(connect(client_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);

    const int server_fd = accept(listen_fd, nullptr, nullptr);
    ASSERT_GE(server_fd, 0);

    const sockaddr_in local = Socket::get_local_addr(server_fd);
    const sockaddr_in peer = Socket::get_peer_addr(server_fd);

    EXPECT_EQ(local.sin_family, AF_INET);
    EXPECT_EQ(peer.sin_family, AF_INET);
    EXPECT_EQ(peer.sin_addr.s_addr, htonl(INADDR_LOOPBACK));

    close(client_fd);
    close(server_fd);
    close(listen_fd);
}

TEST(SocketTest, InvalidFdReturnsError) {
    EXPECT_EQ(Socket::set_non_blocking(-1), -1);
}
