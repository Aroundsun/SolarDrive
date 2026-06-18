#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <sys/epoll.h>

namespace solar_net {

class EventLoop;

// 一个可选择的 I/O 通道，封装了一个文件描述符和它的事件回调
// 每个 Channel 属于一个 EventLoop（它的拥有者循环）
// 不是线程安全的 —— 必须只能从它的拥有者 EventLoop 线程访问
class Channel {
public:
    using EventCallback = std::function<void()>;

    // 构造一个 Channel 用于给定的文件描述符和给定的循环
    Channel(EventLoop* loop, int fd);
    ~Channel();

    // 处理 pending 事件基于 revents_
    void handle_event();

    // 启用读事件
    void enable_reading();

    // 启用写事件
    void enable_writing();

    // 禁写事件
    void disable_writing();

    // 禁用所有事件
    void disable_all();

    // 检查是否没有事件
    bool is_none_event() const;

    // 设置读回调
    void set_read_callback(EventCallback cb)  { read_cb_ = std::move(cb); }

    // 设置写回调
    void set_write_callback(EventCallback cb)  { write_cb_ = std::move(cb); }

    // 设置关闭回调
    void set_close_callback(EventCallback cb)  { close_cb_ = std::move(cb); }
    // 设置错误回调
    void set_error_callback(EventCallback cb)  { error_cb_ = std::move(cb); }

    // 获取文件描述符
    int fd() const { return fd_; }

    // 获取事件
    int events() const { return events_; }

    // 设置发生的事件
    void set_revents(int revt) { revents_ = revt; }

    // 获取发生的事件
    int revents() const { return revents_; }

    // 将这个通道绑定到一个 shared_ptr 拥有者（例如，TcpConnection）
    // 当拥有者被销毁时，通道不会调用回调
    void tie(const std::shared_ptr<void>& obj) {
        tied_ = true;
        tie_ = obj;
    }

    // 用于 EpollPoller 管理通道在 poller 列表中的索引
    int index() const { return index_; }

    // 设置索引
    void set_index(int idx) { index_ = idx; }

    // 获取拥有者 EventLoop
    EventLoop* owner_loop() const { return loop_; }

    // 从 poller 中移除这个通道（必须从拥有者循环调用）
    void remove();

private:
    // 更新兴趣事件在 epoll 中
    void update();

    // 确保在调用回调之前，绑定的对象仍然存活
    void handle_event_with_guard();

    static constexpr int kNoneEvent = 0; // 没有事件
    static constexpr int kReadEvent = EPOLLIN | EPOLLPRI; // 读事件
    static constexpr int kWriteEvent = EPOLLOUT; // 写事件

    EventLoop* loop_; // 拥有者 EventLoop
    int fd_; // 底层的文件描述符
    int events_; // 事件
    int revents_; // 发生的事件
    int index_; // 用于 EpollPoller 管理通道在 poller 列表中的索引

    bool tied_ = false; // 是否绑定到 shared_ptr 拥有者
    std::weak_ptr<void> tie_; // 绑定的对象

    EventCallback read_cb_; // 读回调
    EventCallback write_cb_; // 写回调
    EventCallback close_cb_; // 关闭回调
    EventCallback error_cb_; // 错误回调
};

} // namespace solar_net
