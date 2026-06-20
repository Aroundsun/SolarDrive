#include "acceptor.h"
#include "channel.h"
#include "event_loop.h"
#include "log.h"
#include "socket.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cstdlib>
#include <errno.h>

namespace solar_net {

Acceptor::Acceptor(EventLoop* loop, const ::sockaddr_in& listen_addr)
    : loop_(loop)
    , listening_(false)
    , idle_fd_(::open("/dev/null", O_RDONLY | O_CLOEXEC)) {
    // 创建监听套接字
    int sock_fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (sock_fd < 0) {
        SNLOG_CRITICAL("Acceptor: failed to create socket, errno={}", errno);
        ::abort();
    }

    Socket::set_reuse_addr(sock_fd);

    // 绑定套接字
    int ret = ::bind(sock_fd,
                      reinterpret_cast<const struct sockaddr*>(&listen_addr),
                      sizeof(listen_addr));
    if (ret < 0) {
        SNLOG_CRITICAL("Acceptor: failed to bind, errno={}", errno);
        ::close(sock_fd);
        ::abort();
    }

    socket_ = std::make_unique<Socket>(sock_fd);

    // 创建并设置接受通道(用于接受新的连接)
    channel_ = std::make_unique<Channel>(loop, sock_fd);
    channel_->set_read_callback([this]() { handle_read(); });
}

Acceptor::~Acceptor() {
    channel_->disable_all();
    channel_->remove();
    if (idle_fd_ >= 0) {
        ::close(idle_fd_);
    }
}

void Acceptor::listen() {
    loop_->assert_in_loop_thread();

    int ret = ::listen(socket_->fd(), SOMAXCONN);
    if (ret < 0) {
        SNLOG_ERROR("Acceptor: listen failed, errno={}", errno);
        return;
    }

    channel_->enable_reading();
    // 只在 listen 成功后设置 listening_ 为 true，避免竞争条件
    listening_ = true;
}

void Acceptor::stop_listening() {
    loop_->assert_in_loop_thread();

    if (!listening_) {
        return;
    }

    channel_->disable_all();
    listening_ = false;
}

uint16_t Acceptor::port() const {
    ::sockaddr_in addr = Socket::get_local_addr(socket_->fd());
    return ntohs(addr.sin_port);
}

void Acceptor::handle_read() {
    loop_->assert_in_loop_thread();

    ::sockaddr_in peer_addr{}; // 对端地址
    int conn_fd = accept_one(&peer_addr); 

    while (conn_fd >= 0) {
        if (new_connection_cb_) {
            new_connection_cb_(conn_fd, peer_addr);
        } else {
            ::close(conn_fd);
        }

        // 尝试接受更多的连接
        conn_fd = accept_one(&peer_addr);
    }
}

int Acceptor::accept_one(::sockaddr_in* peer_addr) {
    socklen_t addr_len = sizeof(*peer_addr);
    ::memset(peer_addr, 0, addr_len);

    int conn_fd = ::accept4(socket_->fd(),
                             reinterpret_cast<struct sockaddr*>(peer_addr),
                             &addr_len,
                             SOCK_NONBLOCK | SOCK_CLOEXEC);

    if (conn_fd >= 0) {
        Socket::set_keep_alive(conn_fd);
        Socket::set_tcp_no_delay(conn_fd);
        return conn_fd;
    }

    int saved_errno = errno;
    switch (saved_errno) {
        case EAGAIN:  // EWOULDBLOCK 是 Linux 上的相同值
            // 没有更多的挂起连接
            break;
        case EMFILE:
            // 打开的文件太多 — 关闭空闲文件描述符以释放一个槽,
            // 然后关闭接受的文件描述符,然后重新打开空闲文件描述符
            ::close(idle_fd_);
            idle_fd_ = ::accept(socket_->fd(), nullptr, nullptr);
            if (idle_fd_ >= 0) {
                ::close(idle_fd_);
            }
            idle_fd_ = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
            break;
        default:
            SNLOG_ERROR("Acceptor: accept error, errno={}", saved_errno);
            break;
    }

    return -1;
}

} // namespace solar_net
