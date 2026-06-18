#pragma once

#include <cstdint>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

namespace solar_net {

//封装 socket 的类
class Socket {
public:
    // 构造函数
    explicit Socket(int fd) : fd_(fd) {}

    // 析构函数
    ~Socket() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    // 获取底层的文件描述符
    int fd() const { return fd_; }

    // 禁用拷贝构造
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    // 移动构造函数
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


    // 设置 socket 为非阻塞模式
    // 返回 0 表示成功，-1 表示错误
    static int set_non_blocking(int fd) {
        int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags == -1) {
            return -1;
        }
        return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    // 设置 TCP keep-alive
    // 返回 0 表示成功，-1 表示错误
    static int set_keep_alive(int fd) {
        int val = 1;
        return ::setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val));
    }

    // 设置 TCP_NODELAY
    // 返回 0 表示成功，-1 表示错误
    static int set_tcp_no_delay(int fd) {
        int val = 1;
        return ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));
    }

    // 设置 SO_REUSEADDR
    // 返回 0 表示成功，-1 表示错误
    static int set_reuse_addr(int fd) {
        int val = 1;
        return ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
    }

    // 设置 SO_REUSEPORT
    // 返回 0 表示成功，-1 表示错误
    static int set_reuse_port(int fd) {
        int val = 1;
        return ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &val, sizeof(val));
    }

    // 获取本地地址
    static ::sockaddr_in get_local_addr(int fd) {
        ::sockaddr_in addr{};
        socklen_t len = sizeof(addr);
        ::getsockname(fd, reinterpret_cast<struct sockaddr*>(&addr), &len);
        return addr;
    }

    // 获取对端地址
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
