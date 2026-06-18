#pragma once

#include <spdlog/spdlog.h>

#include <memory>
#include <string>

namespace solar_net::log {

enum class Level {
    Trace,
    Debug,
    Info,
    Warn,
    Err,
    Critical,
    Off,
};

struct Options {
    std::string name = "solar_net";
    Level level = Level::Info;
    bool console = true;
    bool color = true;
    std::string file_path;
};

/// 初始化日志系统（控制台 + 可选文件）。重复调用会先 shutdown 再重建。
bool init(const Options& opts = {});

/// 释放 logger，从 spdlog registry 注销。
void shutdown();

bool is_initialized();

Level level();
void set_level(Level level);

/// 获取底层 spdlog logger；未 init 时返回 nullptr。
std::shared_ptr<spdlog::logger> logger();

} // namespace solar_net::log

#define SNLOG_TRACE(...)                                                       \
    do {                                                                       \
        auto _lg = ::solar_net::log::logger();                                  \
        if (_lg) _lg->trace(__VA_ARGS__);                                       \
    } while (0)

#define SNLOG_DEBUG(...)                                                       \
    do {                                                                       \
        auto _lg = ::solar_net::log::logger();                                  \
        if (_lg) _lg->debug(__VA_ARGS__);                                      \
    } while (0)

#define SNLOG_INFO(...)                                                        \
    do {                                                                       \
        auto _lg = ::solar_net::log::logger();                                  \
        if (_lg) _lg->info(__VA_ARGS__);                                       \
    } while (0)

#define SNLOG_WARN(...)                                                        \
    do {                                                                       \
        auto _lg = ::solar_net::log::logger();                                  \
        if (_lg) _lg->warn(__VA_ARGS__);                                       \
    } while (0)

#define SNLOG_ERROR(...)                                                       \
    do {                                                                       \
        auto _lg = ::solar_net::log::logger();                                  \
        if (_lg) _lg->error(__VA_ARGS__);                                      \
    } while (0)

#define SNLOG_CRITICAL(...)                                                    \
    do {                                                                       \
        auto _lg = ::solar_net::log::logger();                                  \
        if (_lg) _lg->critical(__VA_ARGS__);                                   \
    } while (0)
