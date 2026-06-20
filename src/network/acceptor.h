// =============================================================================
// acceptor.h — SolarDrive 网络层：TCP 监听与 accept
//
// 模块职责：
//   - 创建监听 socket，在 EventLoop 上通过 Channel 监听可读（新连接就绪）
//   - accept 循环直到 EAGAIN；EMFILE 时用 idle_fd_ 技巧丢弃连接并保留 accept 能力
// =============================================================================
#pragma once

#include <netinet/in.h>

#include <cstdint>
#include <functional>
#include <memory>

namespace solar_net {

class Channel;
class EventLoop;
class Socket;

/// 监听 socket 的 Reactor Handler：可读时 accept 并回调 new_connection_cb_。
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
