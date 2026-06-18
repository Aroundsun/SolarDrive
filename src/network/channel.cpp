#include "channel.h"
#include "event_loop.h"

#include <sys/epoll.h>
#include <cassert>

namespace solar_net {

Channel::Channel(EventLoop* loop, int fd)
    : loop_(loop)
    , fd_(fd)
    , events_(kNoneEvent)
    , revents_(kNoneEvent)
    , index_(-1)
    , tied_(false) {}

Channel::~Channel() = default;

void Channel::handle_event() {
    handle_event_with_guard();
}

// 处理事件并确保绑定的对象仍然存活
void Channel::handle_event_with_guard() {
    // 如果绑定到 shared_ptr 拥有者，检查拥有者对象是否仍然存活
    if (tied_) {
        // 获取绑定的对象
        auto guard = tie_.lock();
        if (!guard) {
            // 绑定的对象被销毁，不调用回调
            return;
        }
    }

    // 处理错误事件
    if ((revents_ & EPOLLERR) && error_cb_) {
        error_cb_();
    }

    // 处理挂起 / 关闭事件
    if ((revents_ & (EPOLLHUP)) && !(revents_ & EPOLLIN)) {
        if (close_cb_) {
            close_cb_();
        }
    }

    // 处理可读事件
    if ((revents_ & (EPOLLIN | EPOLLPRI)) && read_cb_) {
        read_cb_();
    }

    // 处理可写事件
    if ((revents_ & EPOLLOUT) && write_cb_) {
        write_cb_();
    }
}

void Channel::enable_reading() {
    events_ |= kReadEvent;
    update();
}

void Channel::enable_writing() {
    events_ |= kWriteEvent;
    update();
}

void Channel::disable_writing() {
    events_ &= ~kWriteEvent;
    update();
}

void Channel::disable_all() {
    events_ = kNoneEvent;
    update();
}

bool Channel::is_none_event() const {
    return events_ == kNoneEvent;
}

void Channel::update() {
    loop_->update_channel(this);
}

void Channel::remove() {
    loop_->remove_channel(this);
}

} // namespace solar_net
