// =============================================================================
// log.cpp — spdlog 控制台/文件 sink 初始化
// =============================================================================
#include "log.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <vector>

namespace solar_net::log {
namespace {

std::shared_ptr<spdlog::logger> g_logger;

spdlog::level::level_enum to_spdlog_level(Level level) {
    switch (level) {
    case Level::Trace:    return spdlog::level::trace;
    case Level::Debug:    return spdlog::level::debug;
    case Level::Info:     return spdlog::level::info;
    case Level::Warn:     return spdlog::level::warn;
    case Level::Err:      return spdlog::level::err;
    case Level::Critical: return spdlog::level::critical;
    case Level::Off:      return spdlog::level::off;
    }
    return spdlog::level::info;
}

Level from_spdlog_level(spdlog::level::level_enum level) {
    switch (level) {
    case spdlog::level::trace:    return Level::Trace;
    case spdlog::level::debug:    return Level::Debug;
    case spdlog::level::info:     return Level::Info;
    case spdlog::level::warn:     return Level::Warn;
    case spdlog::level::err:      return Level::Err;
    case spdlog::level::critical: return Level::Critical;
    case spdlog::level::off:      return Level::Off;
    default:                      return Level::Info;
    }
}

} // namespace

bool init(const Options& opts) {
    shutdown();

    std::vector<spdlog::sink_ptr> sinks;

    if (opts.console) {
        auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        if (!opts.color) {
            sink->set_color_mode(spdlog::color_mode::never);
        }
        sinks.push_back(sink);
    }

    if (!opts.file_path.empty()) {
        sinks.push_back(
            std::make_shared<spdlog::sinks::basic_file_sink_mt>(opts.file_path, true));
    }

    if (sinks.empty()) {
        return false;
    }

    g_logger = std::make_shared<spdlog::logger>(opts.name, sinks.begin(), sinks.end());
    g_logger->set_level(to_spdlog_level(opts.level));
    g_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
    g_logger->flush_on(spdlog::level::warn);

    spdlog::register_logger(g_logger);
    return true;
}

void shutdown() {
    if (!g_logger) {
        return;
    }

    g_logger->flush();

    const std::string name = g_logger->name();
    g_logger.reset();
    spdlog::drop(name);
}

bool is_initialized() {
    return g_logger != nullptr;
}

Level level() {
    if (!g_logger) {
        return Level::Off;
    }
    return from_spdlog_level(g_logger->level());
}

void set_level(Level level) {
    if (g_logger) {
        g_logger->set_level(to_spdlog_level(level));
    }
}

std::shared_ptr<spdlog::logger> logger() {
    return g_logger;
}

} // namespace solar_net::log
