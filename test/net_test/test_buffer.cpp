#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "buffer.h"

using solar_net::Buffer;

TEST(BufferTest, DefaultConstruction) {
    Buffer buf;
    EXPECT_EQ(buf.readable_bytes(), 0u);
    EXPECT_GT(buf.writable_bytes(), 0u);
    EXPECT_EQ(buf.prependable_bytes(), 8u);
}

TEST(BufferTest, AppendAndRetrieve) {
    Buffer buf;
    const std::string payload = "hello";
    buf.append(payload);

    EXPECT_EQ(buf.readable_bytes(), payload.size());
    EXPECT_EQ(buf.retrieve_as_string(5), payload);

    buf.append("world");
    EXPECT_EQ(buf.retrieve_all_as_string(), "world");
    EXPECT_EQ(buf.readable_bytes(), 0u);
}

TEST(BufferTest, AppendRawBytes) {
    Buffer buf;
    const uint8_t bytes[] = {0x01, 0x02, 0x03};
    buf.append(bytes, sizeof(bytes));

    EXPECT_EQ(buf.readable_bytes(), 3u);
    EXPECT_EQ(buf.data()[0], 0x01);
    EXPECT_EQ(buf.data()[2], 0x03);
}

TEST(BufferTest, AppendBuffer) {
    Buffer src;
    src.append("abc");

    Buffer dst;
    dst.append(src);

    EXPECT_EQ(dst.retrieve_all_as_string(), "abc");
    EXPECT_EQ(src.readable_bytes(), 3u);
}

TEST(BufferTest, RetrieveMoreThanAvailableConsumesAll) {
    Buffer buf;
    buf.append("hi");
    buf.retrieve(100);
    EXPECT_EQ(buf.readable_bytes(), 0u);
}

TEST(BufferTest, Int32ReadWrite) {
    Buffer buf;
    const int32_t value = 0x12345678;
    buf.append(reinterpret_cast<const uint8_t*>(&value), sizeof(value));

    EXPECT_EQ(buf.peek_int32(), value);
    EXPECT_EQ(buf.readable_bytes(), sizeof(int32_t));
    EXPECT_EQ(buf.read_int32(), value);
    EXPECT_EQ(buf.readable_bytes(), 0u);
}

TEST(BufferTest, PrependInt32) {
    Buffer buf;
    buf.append(std::string("payload"));

    const int32_t header = 42;
    buf.prepend_int32(header);

    EXPECT_EQ(buf.readable_bytes(), 7u + sizeof(int32_t));
    EXPECT_EQ(buf.read_int32(), header);
    EXPECT_EQ(buf.retrieve_all_as_string(), "payload");
}

TEST(BufferTest, Unwrite) {
    Buffer buf;
    buf.append("abcd");
    buf.unwrite(2);
    EXPECT_EQ(buf.retrieve_all_as_string(), "ab");
}

TEST(BufferTest, Swap) {
    Buffer a;
    a.append("aaa");

    Buffer b;
    b.append("bb");

    a.swap(b);

    EXPECT_EQ(a.retrieve_all_as_string(), "bb");
    EXPECT_EQ(b.retrieve_all_as_string(), "aaa");
}

TEST(BufferTest, Shrink) {
    Buffer buf(64);
    buf.append(std::string(32, 'x'));
    buf.retrieve(28);

    const auto capacity_before = buf.writable_bytes() + buf.readable_bytes() + buf.prependable_bytes();
    buf.shrink();
    const auto capacity_after = buf.writable_bytes() + buf.readable_bytes() + buf.prependable_bytes();

    EXPECT_LT(capacity_after, capacity_before);
    EXPECT_EQ(buf.readable_bytes(), 4u);
    EXPECT_EQ(buf.retrieve_all_as_string(), "xxxx");
}

TEST(BufferTest, EnsureWritableGrows) {
    Buffer buf(4);
    buf.append(std::string(1024, 'z'));

    EXPECT_EQ(buf.readable_bytes(), 1024u);
    EXPECT_EQ(buf.retrieve_all_as_string(), std::string(1024, 'z'));
}

TEST(BufferTest, ReadFromFdFitsInBuffer) {
    int pipefd[2];
    ASSERT_EQ(pipe(pipefd), 0);

    const std::string msg = "pipe data";
    ASSERT_EQ(write(pipefd[1], msg.data(), msg.size()), static_cast<ssize_t>(msg.size()));

    Buffer buf;
    const ssize_t n = buf.read_from_fd(pipefd[0]);
    EXPECT_EQ(n, static_cast<ssize_t>(msg.size()));
    EXPECT_EQ(buf.retrieve_all_as_string(), msg);

    close(pipefd[0]);
    close(pipefd[1]);
}

TEST(BufferTest, ReadFromFdExceedsWritableSpace) {
    int pipefd[2];
    ASSERT_EQ(pipe(pipefd), 0);

    const std::string msg(2048, 'a');
    ASSERT_EQ(write(pipefd[1], msg.data(), msg.size()), static_cast<ssize_t>(msg.size()));

    Buffer buf(16);
    const ssize_t n = buf.read_from_fd(pipefd[0]);
    EXPECT_EQ(n, static_cast<ssize_t>(msg.size()));
    EXPECT_EQ(buf.readable_bytes(), msg.size());
    EXPECT_EQ(buf.retrieve_all_as_string(), msg);

    close(pipefd[0]);
    close(pipefd[1]);
}

TEST(BufferTest, ReadFromFdEof) {
    int pipefd[2];
    ASSERT_EQ(pipe(pipefd), 0);
    close(pipefd[1]);

    Buffer buf;
    EXPECT_EQ(buf.read_from_fd(pipefd[0]), 0);

    close(pipefd[0]);
}

TEST(BufferTest, ReadFromFdNonBlockingEmpty) {
    int pipefd[2];
    ASSERT_EQ(pipe(pipefd), 0);

    const int flags = fcntl(pipefd[0], F_GETFL, 0);
    ASSERT_NE(flags, -1);
    ASSERT_EQ(fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK), 0);

    Buffer buf;
    EXPECT_EQ(buf.read_from_fd(pipefd[0]), -1);
    EXPECT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK);
    EXPECT_EQ(buf.readable_bytes(), 0u);

    close(pipefd[0]);
    close(pipefd[1]);
}

TEST(BufferTest, ShrinkIfNeededAfterRetrieve) {
    Buffer buf(256);
    buf.append(std::string(64, 'q'));
    buf.retrieve(60);

    EXPECT_EQ(buf.readable_bytes(), 4u);
    EXPECT_EQ(buf.retrieve_all_as_string(), "qqqq");
}
