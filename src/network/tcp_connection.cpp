#include "tcp_connection.h"
#include "channel.h"
#include "event_loop.h"
#include "socket.h"

#include <errno.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <cstring>

namespace solar_net {

TcpConnection::TcpConnection(EventLoop* loop,
                               const std::string& name,
                               int fd,
                               const ::sockaddr_in& local_addr,
                               const ::sockaddr_in& peer_addr)
    : loop_(loop)
    , name_(name)
    , state_(State::kConnecting)
    , socket_(std::make_unique<Socket>(fd))
    , channel_(std::make_unique<Channel>(loop, fd))
    , local_addr_(local_addr)
    , peer_addr_(peer_addr) {
    channel_->set_read_callback([this]() { handle_read(0); });
    channel_->set_write_callback([this]() { handle_write(); });
    channel_->set_close_callback([this]() { handle_close(); });
    channel_->set_error_callback([this]() { handle_error(); });
}

TcpConnection::~TcpConnection() = default;

int TcpConnection::fd() const {
    return socket_ ? socket_->fd() : -1;
}

bool TcpConnection::is_connected() const {
    return state_.load(std::memory_order_acquire) == State::kConnected;
}

bool TcpConnection::compare_and_set_state(State expected, State desired) {
    return state_.compare_exchange_strong(expected, desired,
                                          std::memory_order_acq_rel,
                                          std::memory_order_acquire);
}

TcpConnection::State TcpConnection::exchange_state(State desired) {
    return state_.exchange(desired, std::memory_order_acq_rel);
}

void TcpConnection::send(const void* data, std::size_t len) {
    if (!is_connected()) {
        return;
    }

    if (loop_->is_in_loop_thread()) {
        send_in_loop(data, len);
        return;
    }

    std::string msg(static_cast<const char*>(data), len);
    auto self = shared_from_this();
    loop_->run_in_loop([self, msg = std::move(msg)]() {
        if (self->is_connected()) {
            self->send_in_loop(msg);
        }
    });
}

void TcpConnection::send(const std::string& message) {
    if (!is_connected()) {
        return;
    }

    if (loop_->is_in_loop_thread()) {
        send_in_loop(message);
        return;
    }

    auto self = shared_from_this();
    loop_->run_in_loop([self, msg = message]() {
        if (self->is_connected()) {
            self->send_in_loop(msg);
        }
    });
}

void TcpConnection::send(Buffer* buffer) {
    if (!is_connected()) {
        return;
    }

    if (loop_->is_in_loop_thread()) {
        send_in_loop(buffer->data(), buffer->readable_bytes());
        buffer->retrieve_all();
        return;
    }

    std::string msg = buffer->retrieve_all_as_string();
    auto self = shared_from_this();
    loop_->run_in_loop([self, msg = std::move(msg)]() {
        if (self->is_connected()) {
            self->send_in_loop(msg);
        }
    });
}

void TcpConnection::shutdown() {
    if (!compare_and_set_state(State::kConnected, State::kDisconnecting)) {
        return;
    }

    auto self = shared_from_this();
    loop_->run_in_loop([self]() { self->shutdown_in_loop(); });
}

void TcpConnection::force_close() {
    State s = state_.load(std::memory_order_acquire);
    if (s != State::kConnected && s != State::kDisconnecting) {
        return;
    }
    state_.store(State::kDisconnecting, std::memory_order_release);

    auto self = shared_from_this();
    loop_->run_in_loop([self]() { self->force_close_in_loop(); });
}

void TcpConnection::set_tcp_no_delay(bool /*on*/) {
    if (socket_) {
        Socket::set_tcp_no_delay(socket_->fd());
    }
}

void TcpConnection::connection_established() {
    loop_->assert_in_loop_thread();

    State expected = State::kConnecting;
    if (!compare_and_set_state(expected, State::kConnected)) {
        return;
    }

    channel_->tie(shared_from_this());
    channel_->enable_reading();

    if (connection_cb_) {
        connection_cb_(shared_from_this());
    }
}

void TcpConnection::connection_destroyed() {
    loop_->assert_in_loop_thread();

    State s = state_.load(std::memory_order_acquire);
    if (s == State::kConnected) {
        state_.store(State::kDisconnected, std::memory_order_release);
        channel_->disable_all();
    }
    channel_->remove();
}

void TcpConnection::handle_read(int64_t receive_time) {
    loop_->assert_in_loop_thread();

    if (!is_connected()) {
        return;
    }

    ssize_t n = input_buffer_.read_from_fd(socket_->fd());
    if (n > 0) {
        if (message_cb_) {
            message_cb_(shared_from_this(), &input_buffer_, receive_time);
        }
    } else if (n == 0) {
        handle_close();
    } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        handle_error();
    }
}

void TcpConnection::handle_write() {
    loop_->assert_in_loop_thread();

    if (channel_->is_none_event() || !(channel_->events() & EPOLLOUT)) {
        return;
    }

    if (output_buffer_.readable_bytes() > 0) {
        ssize_t n = ::write(socket_->fd(),
                             output_buffer_.data(),
                             output_buffer_.readable_bytes());
        if (n > 0) {
            output_buffer_.retrieve(static_cast<std::size_t>(n));
            if (output_buffer_.readable_bytes() == 0) {
                channel_->disable_writing();

                if (write_complete_cb_) {
                    write_complete_cb_(shared_from_this());
                }

                if (state_.load(std::memory_order_acquire) == State::kDisconnecting) {
                    shutdown_in_loop();
                }
            }
        } else if (n == 0) {
            // 本轮未写出数据，保持 EPOLLOUT，等下次可写重试
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            handle_error();
        }
    }
}

void TcpConnection::handle_close() {
    loop_->assert_in_loop_thread();

    State previous = exchange_state(State::kDisconnected);
    if (previous != State::kConnected && previous != State::kDisconnecting) {
        return;
    }

    channel_->disable_all();

    if (connection_cb_) {
        connection_cb_(shared_from_this());
    }

    if (close_cb_) {
        close_cb_(shared_from_this());
    }
}

void TcpConnection::handle_error() {
    loop_->assert_in_loop_thread();
    handle_close();
}

void TcpConnection::send_in_loop(const void* data, std::size_t len) {
    loop_->assert_in_loop_thread();

    if (!is_connected()) {
        return;
    }

    ssize_t nwrote = 0;
    std::size_t remaining = len;

    if (!channel_->is_none_event() && !(channel_->events() & EPOLLOUT) &&
        output_buffer_.readable_bytes() == 0) {
        nwrote = ::write(socket_->fd(), data, len);
        if (nwrote >= 0) {
            remaining = len - static_cast<std::size_t>(nwrote);
            if (remaining == 0 && write_complete_cb_) {
                write_complete_cb_(shared_from_this());
            }
        } else {
            nwrote = 0;
            if (errno != EWOULDBLOCK && errno != EAGAIN) {
                handle_error();
                return;
            }
            remaining = len;
        }
    }

    if (remaining > 0) {
        std::size_t old_len = output_buffer_.readable_bytes();
        if (old_len + remaining >= high_water_mark_ &&
            old_len < high_water_mark_ &&
            high_water_mark_cb_) {
            high_water_mark_cb_(shared_from_this(), old_len + remaining);
        }

        output_buffer_.append(static_cast<const uint8_t*>(data) + nwrote, remaining);
        if (!channel_->is_none_event() && !(channel_->events() & EPOLLOUT)) {
            channel_->enable_writing();
        }
    }
}

void TcpConnection::send_in_loop(const std::string& message) {
    send_in_loop(message.data(), message.size());
}

void TcpConnection::shutdown_in_loop() {
    loop_->assert_in_loop_thread();

    if (state_.load(std::memory_order_acquire) != State::kDisconnecting) {
        return;
    }

    if (output_buffer_.readable_bytes() == 0) {
        ::shutdown(socket_->fd(), SHUT_WR);
    }
}

void TcpConnection::force_close_in_loop() {
    loop_->assert_in_loop_thread();
    handle_close();
}

} // namespace solar_net
