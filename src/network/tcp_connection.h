// =============================================================================
// tcp_connection.h — SolarDrive 网络层：TCP 连接
//
// 模块职责：
//   - 表示一条已 accept 的 TCP 连接，绑定 Socket + Channel + 输入/输出 Buffer
//   - send/shutdown/force_close 可从任意线程调用，内部 run_in_loop 投递到 IO 线程
//   - shared_ptr + enable_shared_from_this 管理生命周期；Channel::tie 防止析构后回调
//
// Reactor 模式要点：
//   可读 → handle_read 写入 input_buffer_ 并触发 message_cb_
//   可写 → handle_write 从 output_buffer_ 写出；写缓冲非空时 enable_writing 监听 EPOLLOUT
// =============================================================================
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

/// TCP 连接：shared_ptr 管理，在所属 EventLoop 上处理读写与关闭。
class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
public:
    // 连接状态
    enum class State {
        kConnecting,
        kConnected,
        kDisconnecting,
        kDisconnected
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

    TcpConnection(EventLoop* loop,
                   const std::string& name,
                   int fd,
                   const ::sockaddr_in& local_addr,
                   const ::sockaddr_in& peer_addr);

    ~TcpConnection();

    EventLoop* get_loop() const { return loop_; }
    const std::string& name() const { return name_; }
    State state() const { return state_.load(std::memory_order_acquire); }
    int fd() const;

    /// 对端 IPv4 地址（点分十进制）
    std::string peer_ip() const;

    /// 线程安全：跨线程时拷贝数据并 run_in_loop 到 IO 线程发送
    void send(const void* data, std::size_t len);
    void send(const std::string& message);
    void send(Buffer* buffer);

    void shutdown();
    void force_close();
    void set_tcp_no_delay(bool on);

    void set_connection_callback(ConnectionCallback cb) { connection_cb_ = std::move(cb); }
    void set_message_callback(MessageCallback cb) { message_cb_ = std::move(cb); }
    void set_write_complete_callback(WriteCompleteCallback cb) { write_complete_cb_ = std::move(cb); }
    void set_high_water_mark_callback(HighWaterMarkCallback cb, std::size_t mark) {
        high_water_mark_cb_ = std::move(cb);
        high_water_mark_ = mark;
    }
    void set_close_callback(ConnectionCallback cb) { close_cb_ = std::move(cb); }

    Buffer* input_buffer() { return &input_buffer_; }
    Buffer* output_buffer() { return &output_buffer_; }

    void set_context(std::any ctx) { context_ = std::move(ctx); }
    const std::any& get_context() const { return context_; }

    /// 在 IO loop 上启用读监听并触发 connection_cb_（kConnected）
    void connection_established();

    /// 在 IO loop 上移除 Channel、释放 fd 监听
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
