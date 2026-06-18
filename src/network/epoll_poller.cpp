#include "epoll_poller.h"
#include "channel.h"
#include "event_loop.h"
#include "log.h"

#include <unistd.h>
#include <sys/epoll.h>
#include <cerrno>
#include <cstring>
#include <cassert>
#include <cstdlib>

namespace solar_net {

EpollPoller::EpollPoller(EventLoop* loop)
    : owner_loop_(loop)
    , epoll_fd_(::epoll_create1(EPOLL_CLOEXEC))
    , events_(kInitEventListSize) {
    if (epoll_fd_ < 0) {
        SNLOG_CRITICAL("EpollPoller: epoll_create1 failed, errno={}", errno);
        ::abort();
    }
}

EpollPoller::~EpollPoller() {
    ::close(epoll_fd_);
}

int64_t EpollPoller::poll(int timeout_ms, std::vector<Channel*>* active_channels) {
    int num_events = ::epoll_wait(epoll_fd_,
                                   events_.data(),
                                   static_cast<int>(events_.size()),
                                   timeout_ms);

    if (num_events > 0) {
        fill_active_channels(num_events, active_channels);

        // 如果事件列表达到容量，重新调整大小
        if (static_cast<std::size_t>(num_events) == events_.size()) {
            events_.resize(events_.size() * 2);
        }
    } else if (num_events == 0) {
        // 没有发生事件 — 正常超时
    } else {
        if (errno != EINTR) {
            SNLOG_ERROR("EpollPoller: epoll_wait failed, errno={}", errno);
        }
    }

    return 0; // 时间戳占位符，目前未使用，返回 0
}


// 从 epoll 事件数组中填充 active_channels
// @param num_events 事件数
// @param active_channels 有待处理事件的通道
void EpollPoller::fill_active_channels(int num_events,
                                        std::vector<Channel*>* active_channels) {
    // 遍历 epoll 事件数组，填充 active_channels
    for (int i = 0; i < num_events; ++i) {
        Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
        channel->set_revents(static_cast<int>(events_[i].events));
        active_channels->push_back(channel);
    }
}

void EpollPoller::update_channel(Channel* channel) {
    int fd = channel->fd();
    if (channel->is_none_event()) {
        // 如果没有兴趣事件，从 epoll 中移除
        if (has_channel(channel)) {
            epoll_update(EPOLL_CTL_DEL, channel);
            channels_.erase(fd);
        }
    } else {
        if (has_channel(channel)) {
            epoll_update(EPOLL_CTL_MOD, channel);
        } else {
            channels_[fd] = channel;
            epoll_update(EPOLL_CTL_ADD, channel);
        }
    }
}

void EpollPoller::remove_channel(Channel* channel) {
    int fd = channel->fd();
    auto it = channels_.find(fd);
    if (it != channels_.end()) {
        channels_.erase(it);
    }

    if (!channel->is_none_event()) {
        epoll_update(EPOLL_CTL_DEL, channel);
    }
}

bool EpollPoller::has_channel(Channel* channel) const {
    auto it = channels_.find(channel->fd());
    return it != channels_.end() && it->second == channel;
}

void EpollPoller::epoll_update(int operation, Channel* channel) {
    ::epoll_event ev{};
    ev.events = static_cast<uint32_t>(channel->events());
    ev.data.ptr = channel;

    int fd = channel->fd();
    if (::epoll_ctl(epoll_fd_, operation, fd, &ev) < 0) {
        SNLOG_ERROR("EpollPoller: epoll_ctl op={} fd={} errno={}",
                    operation,
                    fd,
                    errno);
    }
}

} // namespace solar_net
