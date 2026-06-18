#pragma once

#include <netinet/in.h>

#include <cstdint>
#include <atomic>
#include <any>
#include <functional>
#include <memory>
#include <string>

#include "buffer.h"

namespace solar_net {

class Channel;
class EventLoop;
class Socket;

/// TCP 连接， shared_ptr 管理。
/// 表示一个已建立的 TCP 连接，带有读/写缓冲区。
class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
public:
    // 连接状态
    enum class State {
        kConnecting, // 连接中
        kConnected, // 已连接
        kDisconnecting, // 断开连接中
        kDisconnected // 断开连接
    };

    // 连接回调
    using ConnectionCallback = std::function<void(const std::shared_ptr<TcpConnection>&)>;
    // 消息回调
    using MessageCallback = std::function<void(const std::shared_ptr<TcpConnection>&,
                                               Buffer*,
                                               int64_t)>;
    // 写完成回调
    using WriteCompleteCallback = std::function<void(const std::shared_ptr<TcpConnection>&)>;
    // 当缓冲区达到阈值时回调
    using HighWaterMarkCallback = std::function<void(const std::shared_ptr<TcpConnection>&,
                                                     std::size_t)>;

    // 构造一个 TcpConnection.
    // @param loop 拥有者 EventLoop.
    // @param name 连接的名称.
    // @param fd 套接字文件描述符.
    // @param local_addr 本地地址.
    // @param peer_addr 对端地址.
    TcpConnection(EventLoop* loop,
                   const std::string& name,
                   int fd,
                   const ::sockaddr_in& local_addr,
                   const ::sockaddr_in& peer_addr);

    ~TcpConnection();

    // 获取拥有者 EventLoop.
    EventLoop* get_loop() const { return loop_; }

    // 获取连接名称.
    const std::string& name() const { return name_; }

    // 获取状态.
    State state() const { return state_.load(std::memory_order_acquire); }

    // 获取底层的文件描述符.
    int fd() const;

    // 发送数据到对端 (线程安全).
    void send(const void* data, std::size_t len);
    // 发送字符串数据到对端.
    void send(const std::string& message);
    // 发送缓冲区数据到对端.
    void send(Buffer* buffer);

    // 关闭写端.
    void shutdown();

    // 强制关闭连接.
    void force_close();

    // 设置 TCP_NODELAY.
    void set_tcp_no_delay(bool on);

    // ---- 设置回调 ----

    // 设置连接回调.
    void set_connection_callback(ConnectionCallback cb) { connection_cb_ = std::move(cb); }
    // 设置消息回调.
    void set_message_callback(MessageCallback cb) { message_cb_ = std::move(cb); }
    // 设置写完成回调.
    void set_write_complete_callback(WriteCompleteCallback cb) { write_complete_cb_ = std::move(cb); }
    // 设置缓冲区达到阈值时回调.
    void set_high_water_mark_callback(HighWaterMarkCallback cb, std::size_t mark) {
        high_water_mark_cb_ = std::move(cb);
        high_water_mark_ = mark;
    }
    // 设置关闭回调.
    void set_close_callback(ConnectionCallback cb) { close_cb_ = std::move(cb); }

    // 获取输入缓冲区.
    Buffer* input_buffer() { return &input_buffer_; }

    // 获取输出缓冲区.
    Buffer* output_buffer() { return &output_buffer_; }

    // 用户自定义上下文（如每条连接的 HttpParser）
    void set_context(std::any ctx) { context_ = std::move(ctx); }
    const std::any& get_context() const { return context_; }

    // 当连接建立时调用.
    void connection_established();

    // 当连接销毁时调用.
    void connection_destroyed();

private:
    // 处理读事件.
    void handle_read(int64_t receive_time);

    // 处理写事件.
    void handle_write();

    // 处理关闭事件.
    void handle_close();

    // 处理错误事件.
    void handle_error();

    // 在循环线程中发送数据.
    void send_in_loop(const void* data, std::size_t len);
    
    void send_in_loop(const std::string& message);

    // 在循环线程中关闭.
    void shutdown_in_loop();

    // 在循环线程中强制关闭.
    void force_close_in_loop();

    bool is_connected() const;
    bool compare_and_set_state(State expected, State desired);
    State exchange_state(State desired);

    EventLoop* loop_; // 拥有者 EventLoop
    std::string name_; // 连接名称
    std::atomic<State> state_; // 连接状态

    std::unique_ptr<Socket> socket_; // 套接字
    std::unique_ptr<Channel> channel_; // 通道

    ::sockaddr_in local_addr_; // 本地地址
    ::sockaddr_in peer_addr_; // 对端地址

    Buffer input_buffer_; // 输入缓冲区
    Buffer output_buffer_; // 输出缓冲区

    ConnectionCallback connection_cb_;
    MessageCallback message_cb_; // 消息回调
    WriteCompleteCallback write_complete_cb_; // 写完成回调
    HighWaterMarkCallback high_water_mark_cb_; // 高水位回调
    std::size_t high_water_mark_ = 64 * 1024 * 1024; // 64 MB default

    ConnectionCallback close_cb_; // 关闭回调
    std::any context_;
};


using TcpConnectionPtr = std::shared_ptr<TcpConnection>;

} // namespace solar_net
