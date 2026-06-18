#pragma once

#include <chrono>
#include <functional>
#include <future>
#include <thread>

#include "event_loop.h"

namespace solar_net::test {

// 在 loop 线程内 delay 后调用 stop()，避免 detached 线程与 loop() 入口 reset stop_ 竞态。
inline void stop_loop_after(EventLoop& loop, std::chrono::milliseconds delay) {
    loop.run_after(std::chrono::duration<double>(delay).count(), [&loop]() {
        loop.stop();
    });
}

// EventLoop 必须在创建它的线程上析构（disable_all/remove 会 assert_in_loop_thread）。
// 在独立线程中创建、运行测试逻辑并销毁 EventLoop。
// 测试逻辑内创建的资源（如 Channel）必须在整个 test_body 作用域内保持存活。
inline void run_in_event_loop_thread(const std::function<void(EventLoop&)>& test_body) {
    std::promise<void> done;
    auto finished = done.get_future();

    std::thread loop_thread([&]() {
        EventLoop loop;
        test_body(loop);
        done.set_value();
    });

    finished.wait();
    loop_thread.join();
}

} // namespace solar_net::test
