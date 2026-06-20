// =============================================================================
// buffer.h — SolarDrive 网络层：网络 I/O 字节缓冲
//
// 模块职责：
//   - 读写双索引环形缓冲（read_index_ / write_index_），预留 kCheapPrepend 便于 prepend
//   - read_from_fd 使用 readv 双缓冲：内核数据先填内部可写区，溢出时用栈上 extrabuf 再 append
// =============================================================================
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
#include <cassert>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <errno.h>

namespace solar_net {

/// 网络 I/O 字节缓冲区，供 TcpConnection 输入/输出使用。
class Buffer {
public:
    Buffer(std::size_t initial_size = 1024)
        : buffer_(kCheapPrepend + initial_size)
        , read_index_(kCheapPrepend)
        , write_index_(kCheapPrepend) {}

    void swap(Buffer& other) {
        buffer_.swap(other.buffer_);
        std::swap(read_index_, other.read_index_);
        std::swap(write_index_, other.write_index_);
    }

    // ---- 读操作 ----

    std::size_t readable_bytes() const {
        return write_index_ - read_index_;
    }

    const uint8_t* data() const {
        return begin() + read_index_;
    }

    uint8_t* data() {
        return begin() + read_index_;
    }

    void retrieve(std::size_t n) {
        if (n <= readable_bytes()) {
            read_index_ += n;
        } else {
            retrieve_all();
        }
        shrink_if_needed();
    }

    void retrieve_all() {
        read_index_ = kCheapPrepend;
        write_index_ = kCheapPrepend;
    }

    std::string retrieve_as_string(std::size_t n) {
        assert(n <= readable_bytes());
        std::string result(reinterpret_cast<const char*>(data()), n);
        retrieve(n);
        return result;
    }

    std::string retrieve_all_as_string() {
        return retrieve_as_string(readable_bytes());
    }

    /// 预览 int32（主机字节序，不消费）
    int32_t peek_int32() const {
        assert(readable_bytes() >= sizeof(int32_t));
        int32_t val = 0;
        std::memcpy(&val, data(), sizeof(val));
        return val;
    }

    int32_t read_int32() {
        int32_t val = peek_int32();
        retrieve(sizeof(val));
        return val;
    }

    // ---- 写操作 ----

    std::size_t writable_bytes() const {
        return buffer_.size() - write_index_;
    }

    void ensure_writable_bytes(std::size_t len) {
        if (writable_bytes() < len) {
            make_space(len);
        }
        assert(writable_bytes() >= len);
    }

    void append(const uint8_t* data, std::size_t len) {
        ensure_writable_bytes(len);
        std::copy(data, data + len, begin() + write_index_);
        has_written(len);
    }

    void append(const std::string& str) {
        append(reinterpret_cast<const uint8_t*>(str.data()), str.size());
    }

    void append(const Buffer& other) {
        append(other.data(), other.readable_bytes());
    }

    void has_written(std::size_t len) {
        assert(len <= writable_bytes());
        write_index_ += len;
    }

    void unwrite(std::size_t len) {
        assert(len <= readable_bytes());
        write_index_ -= len;
    }

    // ---- 前置操作 ----

    void prepend(const uint8_t* data, std::size_t len) {
        assert(len <= prependable_bytes());
        read_index_ -= len;
        std::copy(data, data + len, begin() + read_index_);
    }

    void prepend_int32(int32_t val) {
        prepend(reinterpret_cast<const uint8_t*>(&val), sizeof(val));
    }

    // ---- 杂项 ----

    std::size_t prependable_bytes() const {
        return read_index_;
    }

    void shrink() {
        std::vector<uint8_t> new_buf(kCheapPrepend + readable_bytes());
        std::copy(begin() + read_index_, begin() + write_index_, new_buf.begin() + kCheapPrepend);
        new_buf.swap(buffer_);
        write_index_ = kCheapPrepend + readable_bytes();
        read_index_ = kCheapPrepend;
    }

    /// 从 fd 读取：0=EOF，-1=错误（EAGAIN 时 errno 保留）
    ssize_t read_from_fd(int fd) {
        char extrabuf[65536];

        struct iovec vec[2];
        std::size_t writable = writable_bytes();

        vec[0].iov_base = begin() + write_index_;
        vec[0].iov_len = writable;
        vec[1].iov_base = extrabuf;
        vec[1].iov_len = sizeof(extrabuf);

        int iovcnt = (writable < sizeof(extrabuf)) ? 2 : 1;

        ssize_t n = ::readv(fd, vec, iovcnt);
        if (n < 0) {
            return -1;
        } else if (n == 0) {
            return 0;
        } else if (static_cast<std::size_t>(n) <= writable) {
            has_written(static_cast<std::size_t>(n));
        } else {
            // 超出内部可写区：extrabuf 承接溢出部分再 append
            has_written(writable);
            append(reinterpret_cast<const uint8_t*>(extrabuf),
                   static_cast<std::size_t>(n) - writable);
        }
        return n;
    }

private:
    uint8_t* begin() {
        return buffer_.data();
    }

    const uint8_t* begin() const {
        return buffer_.data();
    }

    void make_space(std::size_t len) {
        if (writable_bytes() + prependable_bytes() < len + kCheapPrepend) {
            // 前置空间 + 尾部可写仍不足，扩容 vector
            buffer_.resize(write_index_ + len);
        } else {
            // 将未读数据前移，复用 prepend 区域，避免频繁 resize
            std::size_t readable = readable_bytes();
            std::copy(begin() + read_index_,
                      begin() + write_index_,
                      begin() + kCheapPrepend);
            read_index_ = kCheapPrepend;
            write_index_ = read_index_ + readable;
            assert(writable_bytes() >= len);
        }
    }

    /// 读索引过半且有效数据占比低时 compact，防止 vector 无界增长
    void shrink_if_needed() {
        if (read_index_ > buffer_.size() / 2 && readable_bytes() < buffer_.size() / 4) {
            shrink();
        }
    }

    static constexpr std::size_t kCheapPrepend = 8;

    std::vector<uint8_t> buffer_;
    std::size_t read_index_;
    std::size_t write_index_;
};

} // namespace solar_net
