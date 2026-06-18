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

// 一个字节缓冲区，用于网络 I/O， 底层 backed by std::vector<uint8_t>
class Buffer {
public:
    // 默认构造函数，初始化时预留一定的空间
    Buffer(std::size_t initial_size = 1024)
        : buffer_(kCheapPrepend + initial_size)
        , read_index_(kCheapPrepend)
        , write_index_(kCheapPrepend) {}

    // 交换两个缓冲区的内容
    void swap(Buffer& other) {
        buffer_.swap(other.buffer_);
        std::swap(read_index_, other.read_index_);
        std::swap(write_index_, other.write_index_);
    }

    // ---- 读操作 ----

    // 可读字节数
    std::size_t readable_bytes() const {
        return write_index_ - read_index_;
    }

    // 指向可读数据的指针
    const uint8_t* data() const {
        return begin() + read_index_;
    }

    // 指向可读数据的指针（可变）
    uint8_t* data() {
        return begin() + read_index_;
    }

    // 从读取端获取 n 个字节
    void retrieve(std::size_t n) {
        if (n <= readable_bytes()) {
            read_index_ += n;
        } else {
            retrieve_all();
        }
        shrink_if_needed();
    }

    // 获取所有可读数据并重置索引
    void retrieve_all() {
        read_index_ = kCheapPrepend;
        write_index_ = kCheapPrepend;
    }

    // 获取 n 个字节的数据并消费
    std::string retrieve_as_string(std::size_t n) {
        assert(n <= readable_bytes());
        std::string result(reinterpret_cast<const char*>(data()), n);
        retrieve(n);
        return result;
    }

    // 获取所有剩余数据并消费
    std::string retrieve_all_as_string() {
        return retrieve_as_string(readable_bytes());
    }

    // 从缓冲区中预览一个 int32_t 数据（假设主机字节序，不消费）
    int32_t peek_int32() const {
        assert(readable_bytes() >= sizeof(int32_t));
        int32_t val = 0;
        std::memcpy(&val, data(), sizeof(val));
        return val;
    }

    // 从缓冲区中读取一个 int32_t 数据并消费
    int32_t read_int32() {
        int32_t val = peek_int32();
        retrieve(sizeof(val));
        return val;
    }

    // ---- 写操作 ----

    // 可写字节数
    std::size_t writable_bytes() const {
        return buffer_.size() - write_index_;
    }

    // 确保至少有 len 个可写字节
    void ensure_writable_bytes(std::size_t len) {
        if (writable_bytes() < len) {
            make_space(len);
        }
        assert(writable_bytes() >= len);
    }

    // 追加原始字节到缓冲区
    void append(const uint8_t* data, std::size_t len) {
        ensure_writable_bytes(len);
        std::copy(data, data + len, begin() + write_index_);
        has_written(len);
    }

    // 追加一个字符串到缓冲区
    void append(const std::string& str) {
        append(reinterpret_cast<const uint8_t*>(str.data()), str.size());
    }

    // 追加另一个缓冲区的可读数据
    void append(const Buffer& other) {
        append(other.data(), other.readable_bytes());
    }

    // 移动写索引 len 个字节（写入缓冲区后）
    void has_written(std::size_t len) {
        assert(len <= writable_bytes());
        write_index_ += len;
    }

    // 从写入端收缩 len 个字节
    void unwrite(std::size_t len) {
        assert(len <= readable_bytes());
        write_index_ -= len;
    }

    // ---- 前置操作 ----

    // 前置数据到读索引前
    void prepend(const uint8_t* data, std::size_t len) {
        assert(len <= prependable_bytes());
        read_index_ -= len;
        std::copy(data, data + len, begin() + read_index_);
    }

    // 前置一个 int32_t 到读索引前
    void prepend_int32(int32_t val) {
        prepend(reinterpret_cast<const uint8_t*>(&val), sizeof(val));
    }

    // ---- 杂项 ----

    // 前置可用字节数
    std::size_t prependable_bytes() const {
        return read_index_;
    }

    // 收缩内部向量以适应当前数据（带有便宜的前置空间）
    void shrink() {
        std::vector<uint8_t> new_buf(kCheapPrepend + readable_bytes());
        std::copy(begin() + read_index_, begin() + write_index_, new_buf.begin() + kCheapPrepend);
        new_buf.swap(buffer_);
        write_index_ = kCheapPrepend + readable_bytes();
        read_index_ = kCheapPrepend;
    }

    // 从文件描述符读取数据（用于 TCP recv）
    // 返回读取的字节数，或 0 表示 EOF，或 -1 表示错误（含 EAGAIN/EWOULDBLOCK，errno 保留）
    ssize_t read_from_fd(int fd) {
        // 使用一个栈缓冲区进行分散读取（readv 优化）
        char extrabuf[65536]; // 64KB stack buffer

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
            return 0; // EOF
        } else if (static_cast<std::size_t>(n) <= writable) {
            // All data fit in the buffer's writable space
            has_written(static_cast<std::size_t>(n));
        } else {
            // Data exceeded writable space — append from extrabuf
            has_written(writable);
            append(reinterpret_cast<const uint8_t*>(extrabuf),
                   static_cast<std::size_t>(n) - writable);
        }
        return n;
    }

private:
    // 指向内部向量的起始位置
    uint8_t* begin() {
        return buffer_.data();
    }

    const uint8_t* begin() const {
        return buffer_.data();
    }

    // 确保至少有 len 个可写字节
    void make_space(std::size_t len) {
        if (writable_bytes() + prependable_bytes() < len + kCheapPrepend) {
            // Need to grow
            buffer_.resize(write_index_ + len);
        } else {
            // Move data forward to reuse prepend space
            std::size_t readable = readable_bytes();
            std::copy(begin() + read_index_,
                      begin() + write_index_,
                      begin() + kCheapPrepend);
            read_index_ = kCheapPrepend;
            write_index_ = read_index_ + readable;
            assert(writable_bytes() >= len);
        }
    }

    // 如果读索引 far ahead, 重置索引以避免无界增长
    void shrink_if_needed() {
        if (read_index_ > buffer_.size() / 2 && readable_bytes() < buffer_.size() / 4) {
            shrink();
        }
    }

    static constexpr std::size_t kCheapPrepend = 8;

    std::vector<uint8_t> buffer_; // 内部向量
    std::size_t read_index_; // 读索引
    std::size_t write_index_; // 写索引
};

} // namespace solar_net
