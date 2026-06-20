// =============================================================================
// epoll_poller.h — SolarDrive 网络层：epoll I/O 多路复用器
//
// 模块职责：
//   - 封装 epoll_create1 / epoll_wait / epoll_ctl，是 Reactor 的 Demultiplexer
//   - 维护 fd → Channel* 映射；epoll_event.data.ptr 直接存 Channel 指针，避免查表
//
// Reactor 模式要点：
//   poll() 阻塞等待就绪 fd，将 revents 写入各 Channel 后返回 active_channels_ 供 EventLoop 分发。
// =============================================================================
#pragma once

#include <sys/epoll.h>

#include <vector>
#include <map>
#include <cstdint>

namespace solar_net {

class Channel;
class EventLoop;

/// epoll 多路复用器，隶属于唯一 EventLoop，非线程安全。
class EpollPoller {
public:
    // 构造一个 poller 用于给定的 event loop
    explicit EpollPoller(EventLoop* loop);

    ~EpollPoller();

    // 等待 I/O 事件。
    int64_t poll(int timeout_ms, std::vector<Channel*>* active_channels);

    // 更新一个通道的兴趣事件（添加/修改）
    void update_channel(Channel* channel);

    // 从 epoll 监控中移除一个通道
    void remove_channel(Channel* channel);

    // 检查一个通道是否注册在这个 poller 中
    bool has_channel(Channel* channel) const;

private:
    // 更新 epoll 与给定的操作（EPOLL_CTL_ADD/MOD/DEL）
    void epoll_update(int operation, Channel* channel);

    // 从 epoll 事件数组中填充 active_channels
    void fill_active_channels(int num_events, std::vector<Channel*>* active_channels);

    static constexpr int kInitEventListSize = 16;

    EventLoop* owner_loop_; // 拥有者 EventLoop
    int epoll_fd_; // epoll 文件描述符

    // epoll_wait 使用的 epoll_event 数组
    std::vector<::epoll_event> events_;

    // 从 fd 到 Channel* 的映射
    std::map<int, Channel*> channels_;
};

} // namespace solar_net
