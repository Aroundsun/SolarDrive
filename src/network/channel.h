// =============================================================================
// channel.h — SolarDrive 网络层：I/O 事件通道
//
// 模块职责：
//   - 封装 fd 与 epoll 兴趣事件（events_）及本次就绪事件（revents_）
//   - 注册读/写/关闭/错误回调，由 EventLoop 在 epoll 返回后调用 handle_event
//   - tie() 将 Channel 生命周期与 TcpConnection 等 shared_ptr 绑定，防止对象已析构仍触发回调
//
// Reactor 模式要点：
//   Channel 是 Reactor 中「事件 demultiplexing 后的 Handler」；
//   修改 events_ 后通过 update() 通知 EpollPoller 执行 EPOLL_CTL_ADD/MOD/DEL。
// =============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <sys/epoll.h>

namespace solar_net {

class EventLoop;

/// I/O 事件通道：fd + 兴趣事件 + 回调，隶属于唯一 EventLoop，非线程安全。
class Channel {
public:
    using EventCallback = std::function<void()>;

    Channel(EventLoop* loop, int fd);
    ~Channel();

    /// 根据 EpollPoller 写入的 revents_ 依次触发错误/关闭/读/写回调
    void handle_event();

    void enable_reading();
    void enable_writing();
    void disable_writing();
    void disable_all();
    bool is_none_event() const;

    void set_read_callback(EventCallback cb)  { read_cb_ = std::move(cb); }
    void set_write_callback(EventCallback cb)  { write_cb_ = std::move(cb); }
    void set_close_callback(EventCallback cb)  { close_cb_ = std::move(cb); }
    void set_error_callback(EventCallback cb)  { error_cb_ = std::move(cb); }

    int fd() const { return fd_; }
    int events() const { return events_; }
    void set_revents(int revt) { revents_ = revt; }
    int revents() const { return revents_; }

    /// 绑定 shared_ptr 拥有者；回调前 lock 失败则跳过，避免 TcpConnection 已析构仍执行回调
    void tie(const std::shared_ptr<void>& obj) {
        tied_ = true;
        tie_ = obj;
    }

    int index() const { return index_; }
    void set_index(int idx) { index_ = idx; }
    EventLoop* owner_loop() const { return loop_; }

    void remove();

private:
    // 更新兴趣事件在 epoll 中
    void update();

    // 确保在调用回调之前，绑定的对象仍然存活
    void handle_event_with_guard();

    static constexpr int kNoneEvent = 0;
    static constexpr int kReadEvent = EPOLLIN | EPOLLPRI;
    static constexpr int kWriteEvent = EPOLLOUT;

    EventLoop* loop_;
    int fd_;
    int events_;
    int revents_;
    int index_;

    bool tied_ = false;
    std::weak_ptr<void> tie_;

    EventCallback read_cb_;
    EventCallback write_cb_;
    EventCallback close_cb_;
    EventCallback error_cb_;
};

} // namespace solar_net
