// =============================================================================
// socket.h — SolarDrive 网络层：socket fd RAII 与常用选项
//
// 模块职责：
//   - 封装 fd 的 RAII 关闭与移动语义
//   - 提供非阻塞、keep-alive、NODELAY、REUSEADDR 等静态工具方法
// =============================================================================
#pragma once

#include <cstdint>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

namespace solar_net {

/// socket fd 的 RAII 包装及 setsockopt 工具函数。
class Socket {
public:
    explicit Socket(int fd) : fd_(fd) {}

    ~Socket() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    int fd() const { return fd_; }

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    Socket(Socket&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }

    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) {
                ::close(fd_);
            }
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    static int set_non_blocking(int fd) {
        int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags == -1) {
            return -1;
        }
        return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    static int set_keep_alive(int fd) {
        int val = 1;
        return ::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val));
    }

    static int set_tcp_no_delay(int fd) {
        int val = 1;
        return ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));
    }

    static int set_reuse_addr(int fd) {
        int val = 1;
        return ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
    }

    static int set_reuse_port(int fd) {
        int val = 1;
        return ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &val, sizeof(val));
    }

    static ::sockaddr_in get_local_addr(int fd) {
        ::sockaddr_in addr{};
        socklen_t len = sizeof(addr);
        ::getsockname(fd, reinterpret_cast<struct sockaddr*>(&addr), &len);
        return addr;
    }

    static ::sockaddr_in get_peer_addr(int fd) {
        ::sockaddr_in addr{};
        socklen_t len = sizeof(addr);
        ::getpeername(fd, reinterpret_cast<struct sockaddr*>(&addr), &len);
        return addr;
    }

private:
    int fd_;
};

} // namespace solar_net
