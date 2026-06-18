#pragma once

#include <netinet/in.h>

#include <cstdint>
#include <functional>
#include <memory>

namespace solar_net {

class Channel;
class EventLoop;
class Socket;

/// 在指定地址上监听并接受新的 TCP 连接。
class Acceptor {
public:
    using NewConnectionCallback = std::function<void(int sockfd, const ::sockaddr_in& peer_addr)>;

    Acceptor(EventLoop* loop, const ::sockaddr_in& listen_addr);
    ~Acceptor();

    void set_new_connection_callback(NewConnectionCallback cb) {
        new_connection_cb_ = std::move(cb);
    }

    void listen();

    /// 停止接受新连接 (必须在 loop 线程调用)。
    void stop_listening();

    uint16_t port() const;

    bool listening() const { return listening_; }

private:
    void handle_read();
    int accept_one(::sockaddr_in* peer_addr);

    EventLoop* loop_;
    bool listening_;
    int idle_fd_;
    std::unique_ptr<Socket> socket_;
    std::unique_ptr<Channel> channel_;
    NewConnectionCallback new_connection_cb_;
};

} // namespace solar_net
