#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "socket.h"

namespace solar_net::test {

struct TcpSocketPair {
    int listen_fd = -1;
    int client_fd = -1;
    int server_fd = -1;
    ::sockaddr_in server_local{};
    ::sockaddr_in server_peer{};
};

inline TcpSocketPair make_tcp_socket_pair() {
    TcpSocketPair pair;

    pair.listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (pair.listen_fd < 0) {
        return pair;
    }

    Socket::set_reuse_addr(pair.listen_fd);

    ::sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0);

    if (::bind(pair.listen_fd, reinterpret_cast<::sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(pair.listen_fd);
        pair.listen_fd = -1;
        return pair;
    }

    if (::listen(pair.listen_fd, 1) != 0) {
        ::close(pair.listen_fd);
        pair.listen_fd = -1;
        return pair;
    }

    ::socklen_t len = sizeof(addr);
    if (::getsockname(pair.listen_fd, reinterpret_cast<::sockaddr*>(&addr), &len) != 0) {
        ::close(pair.listen_fd);
        pair.listen_fd = -1;
        return pair;
    }

    pair.client_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (pair.client_fd < 0) {
        ::close(pair.listen_fd);
        pair.listen_fd = -1;
        return pair;
    }

    if (::connect(pair.client_fd, reinterpret_cast<::sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(pair.client_fd);
        ::close(pair.listen_fd);
        pair.client_fd = -1;
        pair.listen_fd = -1;
        return pair;
    }

    pair.server_fd = ::accept(pair.listen_fd, nullptr, nullptr);
    ::close(pair.listen_fd);
    pair.listen_fd = -1;

    if (pair.server_fd < 0) {
        ::close(pair.client_fd);
        pair.client_fd = -1;
        return pair;
    }

    pair.server_local = Socket::get_local_addr(pair.server_fd);
    pair.server_peer = Socket::get_peer_addr(pair.server_fd);
    Socket::set_non_blocking(pair.server_fd);

    return pair;
}

inline ::sockaddr_in make_loopback_addr(uint16_t port = 0) {
    ::sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    return addr;
}

inline int connect_to(uint16_t port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    ::sockaddr_in addr = make_loopback_addr(port);
    if (::connect(fd, reinterpret_cast<::sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return -1;
    }

    return fd;
}

inline void close_tcp_socket_pair(TcpSocketPair& pair) {
    if (pair.client_fd >= 0) {
        ::close(pair.client_fd);
        pair.client_fd = -1;
    }
    if (pair.server_fd >= 0) {
        ::close(pair.server_fd);
        pair.server_fd = -1;
    }
    if (pair.listen_fd >= 0) {
        ::close(pair.listen_fd);
        pair.listen_fd = -1;
    }
}

} // namespace solar_net::test
