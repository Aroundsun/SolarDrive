// =============================================================================
// tcp_server.h — SolarDrive 网络层：多线程 TCP 服务器
//
// 模块职责：
//   - 主 EventLoop 运行 Acceptor 接受新连接
//   - EventLoopThreadPool 提供 IO 线程，新连接 round-robin 分配到各 IO EventLoop
//   - 维护 connections_ 映射，协调跨线程的连接建立与销毁
//
// Reactor 模式要点：
//   accept 在主 loop，读写在各 IO loop；跨线程通过 run_in_loop / queue_in_loop 投递。
// =============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <netinet/in.h>

namespace solar_net {

class EventLoop;
class EventLoopThreadPool;
class Acceptor;
class TcpConnection;
class Buffer;

/// 多线程 Reactor TCP 服务器：主 loop 接受连接，IO 线程池处理读写。
class TcpServer {
public:
    // 连接回调
    using ConnectionCallback = std::function<void(const std::shared_ptr<TcpConnection>&)>;
    // 消息回调
    using MessageCallback = std::function<void(const std::shared_ptr<TcpConnection>&,
                                               Buffer*,
                                               int64_t)>;
    // 写完成回调
    using WriteCompleteCallback = std::function<void(const std::shared_ptr<TcpConnection>&)>;
    // 线程初始化回调
    using ThreadInitCallback = std::function<void(EventLoop*)>;

    /// 构造一个 TcpServer。
    /// @param loop 接受器的 EventLoop (主循环)。
    /// @param listen_addr 监听地址。
    /// @param name 服务器名称。
    TcpServer(EventLoop* loop,
              const ::sockaddr_in& listen_addr,
              const std::string& name);

    /// 析构时调用 stop()，遵守与 stop() 相同的线程约束。
    ~TcpServer();

    /// 启动服务器 (监听 + 启动 IO 线程池)。
    void start();

    /// 停止服务器：停止 accept、关闭所有连接、停止 IO 线程池。
    ///
    /// 调用约束：
    /// - loop.loop() 仍在运行时，可从任意线程调用（通过 run_in_loop 投递到 loop 线程）。
    /// - loop.loop() 已返回后，必须在 loop 所属线程调用；否则仍会走 run_in_loop，
    ///   但主 loop 已不再跑，关停任务可能永远不执行。
    ///
    /// 一次性关停：内部会 thread_pool_.reset()，无重建线程池路径，stop() 后不可再 start()。
    ///
    /// 推荐写法见 example/echo_server.cpp：loop.loop() 返回后在同一线程显式 stop()。
    void stop();

    /// 设置 IO 线程的数量 (必须在 start() 之前调用)。
    void set_thread_num(int num_threads);

    // ---- 设置回调 ----

    /// 设置连接回调。
    /// 建立（state==kConnected）与断开（state==kDisconnected）时都会调用；
    /// 业务侧须用 conn->state() 区分，不可假定仅在新连接时触发。
    void set_connection_callback(ConnectionCallback cb) { connection_cb_ = std::move(cb); }
    /// 设置消息回调。
    void set_message_callback(MessageCallback cb) { message_cb_ = std::move(cb); }
    /// 设置写完成回调。
    void set_write_complete_callback(WriteCompleteCallback cb) { write_complete_cb_ = std::move(cb); }
    /// 设置线程初始化回调。
    void set_thread_init_callback(ThreadInitCallback cb) { thread_init_cb_ = std::move(cb); }

    /// 获取主 EventLoop。
    EventLoop* get_loop() const { return loop_; }

    /// 获取 IO 线程池。
    std::shared_ptr<EventLoopThreadPool> thread_pool() const { return thread_pool_; }

    /// 获取监听端口。
    uint16_t port() const;

private:
    /// 当接受器接受一个新的连接时调用。
    void new_connection(int fd, const ::sockaddr_in& peer_addr);

    /// 从映射中删除一个连接。
    void remove_connection(const std::shared_ptr<TcpConnection>& conn);

    /// 在 loop 线程内停止服务器。
    void stop_in_loop();

    /// 在 loop 线程内从映射中删除连接并销毁 Channel。
    void remove_connection_in_loop(const std::shared_ptr<TcpConnection>& conn);

    EventLoop* loop_; // 主 EventLoop
    std::string name_; // 服务器名称
    std::unique_ptr<Acceptor> acceptor_; // 接受器

    ConnectionCallback connection_cb_; // 连接回调
    MessageCallback message_cb_; // 消息回调
    WriteCompleteCallback write_complete_cb_; // 写完成回调
    ThreadInitCallback thread_init_cb_; // 线程初始化回调

    std::map<std::string, std::shared_ptr<TcpConnection>> connections_;
    int next_conn_id_ = 1;

    std::shared_ptr<EventLoopThreadPool> thread_pool_; // IO 线程池

    bool started_ = false; // 是否已启动
};

} // namespace solar_net
