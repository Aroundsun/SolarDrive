// =============================================================================
// timestamp.h — SolarDrive 网络层：单调时钟时间戳
//
// 模块职责：
//   - 基于 steady_clock 的时间点，供定时器与超时计算（不受系统时间调整影响）
// =============================================================================
#pragma once

#include <chrono>

namespace solar_net {

using Timestamp = std::chrono::steady_clock::time_point;

inline Timestamp now() {
    return std::chrono::steady_clock::now();
}

inline Timestamp add_time(Timestamp when, double seconds) {
    using DoubleDuration = std::chrono::duration<double>;
    return when + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                      DoubleDuration(seconds));
}

} // namespace solar_net
