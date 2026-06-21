// =============================================================================
// main.cpp — SolarDrive 进程入口
//
// 职责：加载配置、初始化存储/数据库/Redis、注册 HTTP 路由、启动 Reactor 网络服务。
// 请求链路：TcpConnection → HttpParser → 静态资源 / 鉴权中间件 → HttpRouter → Handler
// =============================================================================

#include <memory>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <ifaddrs.h>
#include <limits.h>
#include <optional>
#include <string>
#include <unistd.h>
#include <arpa/inet.h>

#include "network/tcp_server.h"
#include "network/event_loop.h"
#include "network/tcp_connection.h"
#include "network/socket.h"
#include "network/log.h"
#include "http/http_request.h"
#include "http/http_response.h"
#include "http/http_parser.h"
#include "http/http_router.h"
#include "http/static_handler.h"
#include "storage/object_store.h"
#include "metadata/db_pool.h"
#include "metadata/schema.h"
#include "metadata/file_dao.h"
#include "metadata/folder_dao.h"
#include "metadata/content_dao.h"
#include "metadata/content_gc.h"
#include "metadata/share_dao.h"
#include "api/download_handler.h"
#include "api/file_handler.h"
#include "api/route_registry.h"
#include "api/auth_handler.h"
#include "api/multipart_upload_handler.h"
#include "api/share_handler.h"
#include "api/folder_handler.h"
#include "auth/jwt.h"
#include "auth/auth_middleware.h"
#include "auth/user_dao.h"
#include "cache/redis_client.h"
#include "cache/rate_limiter.h"
#include "config/config.h"
#include "monitor/metrics.h"
#include "ws/ws_upgrade.h"
#include "ws/ws_handler.h"
#include "ws/ws_session.h"

#include <pqxx/except>

namespace {

constexpr int kMaxKeepAliveRequests = 1000;

struct HttpConnContext {
    std::shared_ptr<solar_http::HttpParser> parser;
    int request_count = 0;
};

// 检查 Web 根目录下是否存在 index.html
bool web_root_has_index(const std::string& root) {
    return std::ifstream(root + "/index.html").good();
}

// 解析 Web 静态资源目录：环境变量 → 当前目录 web → 可执行文件旁 ../web
std::string resolve_web_root(const char* env_path) {
    if (env_path && env_path[0] != '\0' && web_root_has_index(env_path)) {
        return env_path;
    }
    if (web_root_has_index("web")) {
        return "web";
    }

    char exe[PATH_MAX];
    const ssize_t n = ::readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        exe[n] = '\0';
        std::string dir(exe);
        const auto slash = dir.rfind('/');
        if (slash != std::string::npos) {
            char resolved[PATH_MAX];
            const std::string candidate = dir.substr(0, slash) + "/../web";
            if (::realpath(candidate.c_str(), resolved) && web_root_has_index(resolved)) {
                return resolved;
            }
        }
    }

    return env_path && env_path[0] != '\0' ? env_path : "web";
}

struct CliOptions {
    std::string config_path = "config/config.yaml";
    bool config_explicit = false;          // 用户是否显式指定配置文件
    std::optional<uint16_t> port_override; // 命令行端口覆盖 YAML
};

void print_usage(const char* prog) {
    std::cout
        << "Usage: " << prog << " [options] [port]\n"
        << "Options:\n"
        << "  -c, --config <path>   YAML config file (default: config/config.yaml)\n"
        << "  -h, --help            Show this help\n"
        << "\n"
        << "Environment overrides (take precedence over YAML):\n"
        << "  SOLAR_CONFIG          Default config file path\n"
        << "  SOLAR_DB              PostgreSQL connection string\n"
        << "  SOLAR_STORE           Object storage base path\n"
        << "  SOLAR_REDIS_HOST      Redis host\n"
        << "  SOLAR_REDIS_PORT      Redis port\n"
        << "  SOLAR_JWT_SECRET      JWT signing secret\n"
        << "  SOLAR_WEB             Web UI root directory\n";
}

CliOptions parse_cli(int argc, char* argv[]) {
    CliOptions opts;

    if (const char* env = std::getenv("SOLAR_CONFIG")) {
        opts.config_path = env;
        opts.config_explicit = true;
    }

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            std::exit(0);
        }
        if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            opts.config_path = argv[++i];
            opts.config_explicit = true;
            continue;
        }
        if (!arg.empty() && arg[0] != '-') {
            try {
                const int port = std::stoi(arg);
                if (port > 0 && port <= 65535) {
                    opts.port_override = static_cast<uint16_t>(port);
                }
            } catch (...) {
                // ignore unknown positional args
            }
        }
    }

    if (!opts.config_explicit &&
        std::ifstream("config/config.local.yaml").good()) {
        opts.config_path = "config/config.local.yaml";
    }

    return opts;
}

solar_net::log::Level parse_log_level(const std::string& level) {
    if (level == "trace")    return solar_net::log::Level::Trace;
    if (level == "debug")    return solar_net::log::Level::Debug;
    if (level == "warn")     return solar_net::log::Level::Warn;
    if (level == "error" || level == "err") return solar_net::log::Level::Err;
    if (level == "critical") return solar_net::log::Level::Critical;
    if (level == "off")      return solar_net::log::Level::Off;
    return solar_net::log::Level::Info;
}

// 环境变量优先级高于 YAML 配置
void apply_env_overrides(solar_config::AppConfig& cfg, std::string& db_conn) {
    db_conn = cfg.database.conn_str();

    if (const char* v = std::getenv("SOLAR_DB")) {
        db_conn = v;
    }
    if (const char* v = std::getenv("SOLAR_STORE")) {
        cfg.storage.base_path = v;
    }
    if (const char* v = std::getenv("SOLAR_REDIS_HOST")) {
        cfg.redis.host = v;
    }
    if (const char* v = std::getenv("SOLAR_REDIS_PORT")) {
        cfg.redis.port = static_cast<uint16_t>(std::atoi(v));
    }
    if (const char* v = std::getenv("SOLAR_JWT_SECRET")) {
        cfg.jwt.secret = v;
    }
}

bool init_logging(const solar_config::LogConfig& log_cfg) {
    if (!log_cfg.file.empty()) {
        const auto parent = std::filesystem::path(log_cfg.file).parent_path();
        if (!parent.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
        }
    }

    solar_net::log::Options opts;
    opts.name = "solardrive";
    opts.level = parse_log_level(log_cfg.level);
    opts.console = true;
    opts.color = true;
    opts.file_path = log_cfg.file;
    return solar_net::log::init(opts);
}

// 统计每次 HTTP 响应：总请求数 + 错误数（供 Prometheus）
void record_response_metrics(const solar_http::HttpResponse& resp) {
    solar_monitor::Metrics::inc_requests();
    if (resp.status_code_ >= 400) {
        solar_monitor::Metrics::inc_errors();
    }
}

void log_access_urls(uint16_t port) {
    SNLOG_INFO("web UI (VM local):     http://127.0.0.1:{}/", port);
    SNLOG_INFO("metrics UI (VM local): http://127.0.0.1:{}/metrics.html", port);
    SNLOG_INFO("metrics API (VM local): http://127.0.0.1:{}/metrics", port);

    struct ifaddrs* ifap = nullptr;
    if (getifaddrs(&ifap) != 0) {
        return;
    }

    for (auto* ifa = ifap; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }

        char ip[INET_ADDRSTRLEN] = {};
        const auto* sin = reinterpret_cast<sockaddr_in*>(ifa->ifa_addr);
        if (!inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip))) {
            continue;
        }
        if (std::strcmp(ip, "127.0.0.1") == 0) {
            continue;
        }

        SNLOG_INFO("web UI (LAN/host):     http://{}:{}/", ip, port);
        SNLOG_INFO("metrics UI (LAN/host): http://{}:{}/metrics.html", ip, port);
        SNLOG_INFO("metrics API (LAN/host): http://{}:{}/metrics", ip, port);
    }

    freeifaddrs(ifap);
}

} // namespace

// 全局指针，供信号处理函数优雅退出
solar_net::EventLoop* g_main_loop = nullptr;
solar_net::TcpServer* g_server   = nullptr;

// SIGINT / SIGTERM：停止 accept 与事件循环
void signal_handler(int) {
    if (solar_net::log::is_initialized()) {
        SNLOG_INFO("received shutdown signal");
    } else {
        std::cout << "\n[main] received SIGINT, shutting down...\n";
    }
    if (g_server)    g_server->stop();
    if (g_main_loop) g_main_loop->stop();
}

int main(int argc, char* argv[]) {
    // ---- 1. 配置与日志 ----
    const CliOptions cli = parse_cli(argc, argv);
    solar_config::AppConfig cfg = solar_config::AppConfig::load_from_file(cli.config_path);

    std::string db_conn;
    apply_env_overrides(cfg, db_conn);

    if (cli.port_override) {
        cfg.server.port = *cli.port_override;
    }

    if (!init_logging(cfg.logging)) {
        std::cerr << "[main] WARN: logging init failed, using stderr only\n";
    }

    const std::string web_dir = resolve_web_root(std::getenv("SOLAR_WEB"));
    if (!web_root_has_index(web_dir)) {
        SNLOG_ERROR("web UI not found at \"{}/index.html\"", web_dir);
        return 1;
    }

    SNLOG_INFO("config loaded from {}", cli.config_path);
    SNLOG_INFO("listening port: {}", cfg.server.port);
    SNLOG_INFO("worker threads: {}", cfg.server.threads);
    SNLOG_INFO("storage path: {}", cfg.storage.base_path);
    SNLOG_INFO("chunk_size: {} bytes", cfg.storage.chunk_size);
    SNLOG_INFO("max_file_size: {} MB", cfg.limits.max_file_size_mb);
    SNLOG_INFO("jwt ttl: {} hours", cfg.jwt.ttl_hours);
    SNLOG_INFO("database: {}:{}/{}", cfg.database.host, cfg.database.port, cfg.database.dbname);
    SNLOG_INFO("redis: {}:{}", cfg.redis.host, cfg.redis.port);
    SNLOG_INFO("web root: {}", web_dir);

    // ---- 2. 存储与元数据层 ----
    solar_storage::ObjectStore store(cfg.storage.base_path, cfg.storage.chunk_size);

    std::unique_ptr<solar_metadata::DbPool> db_pool;
    try {
        pqxx::connection probe(db_conn);
        db_pool = std::make_unique<solar_metadata::DbPool>(db_conn, cfg.database.pool_size);
    } catch (const pqxx::failure& e) {
        SNLOG_CRITICAL("cannot connect to PostgreSQL: {}", e.what());
        solar_net::log::shutdown();
        std::_Exit(1);
    }

    try {
        if (solar_metadata::bootstrap_schema(*db_pool)) {
            SNLOG_WARN("database migrated to schema v2: file metadata tables were dropped and recreated");
        }
        SNLOG_INFO("database schema ready (v2)");
    } catch (const std::exception& e) {
        SNLOG_CRITICAL("bootstrap_schema failed: {}", e.what());
        solar_net::log::shutdown();
        std::_Exit(1);
    }

    solar_metadata::ContentDao content_dao(*db_pool);
    solar_metadata::FileDao file_dao(*db_pool, content_dao);
    solar_metadata::FolderDao folder_dao(*db_pool);
    solar_metadata::ShareDao share_dao(*db_pool);
    solar_metadata::ContentGc content_gc(content_dao, *db_pool, store);

    // ---- 3. Redis 缓存与 JWT ----
    auto redis = std::make_shared<solar_cache::RedisClient>(
        cfg.redis.host, cfg.redis.port);
    if (redis->ping()) {
        SNLOG_INFO("Redis connected");
    } else {
        SNLOG_WARN("Redis not available, continuing without cache");
    }

    solar_cache::RateLimiter rate_limiter(redis, cfg.limits.rate_limit_per_ip);
    if (cfg.limits.rate_limit_per_ip > 0) {
        SNLOG_INFO("rate limit enabled: {} requests/min per IP (Redis sliding window)",
                   cfg.limits.rate_limit_per_ip);
    }

    solar_auth::JwtUtil::set_secret(cfg.jwt.secret);

    auto user_dao = std::make_shared<solar_auth::UserDao>(*db_pool);

    // ---- 4. 业务 Handler 实例 ----
    solar_api::AuthHandler auth_handler(user_dao, cfg.jwt.ttl_hours);
    solar_api::DownloadHandler download_handler(store, file_dao);
    solar_api::MultipartUploadHandler multipart_handler(
        store, file_dao, folder_dao, redis,
        cfg.storage.chunk_size, cfg.limits.max_file_size_bytes());

    solar_api::ShareHandler share_handler(share_dao, file_dao, store, redis);
    solar_api::FolderHandler folder_handler(folder_dao, file_dao);
    solar_api::FileHandler file_handler(file_dao, share_dao, content_gc);

    const solar_api::AppServices services{
        cfg,
        store,
        file_dao,
        folder_dao,
        redis,
        auth_handler,
        download_handler,
        multipart_handler,
        share_handler,
        folder_handler,
        file_handler,
    };

    // ---- 5. 网络服务：主 EventLoop + 多线程 TcpServer ----
    solar_net::EventLoop main_loop;
    g_main_loop = &main_loop;

    ::sockaddr_in listen_addr;
    std::memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family      = AF_INET;
    listen_addr.sin_addr.s_addr = INADDR_ANY;
    listen_addr.sin_port        = ::htons(cfg.server.port);

    solar_net::TcpServer server(&main_loop, listen_addr, "SolarDrive");
    server.set_thread_num(cfg.server.threads);
    g_server = &server;

    auto router = std::make_shared<solar_http::HttpRouter>();
    auto static_handler = std::make_shared<solar_http::StaticHandler>(web_dir);
    solar_api::register_api_routes(router, services);

    // ---- 7. 连接回调：HTTP 解析 或 WebSocket 升级后的 WsHandler ----
    server.set_connection_callback(
        [router, static_handler, redis = services.redis, rate_limiter = &rate_limiter](
            const solar_net::TcpConnectionPtr& conn) {
            if (conn->state() == solar_net::TcpConnection::State::kConnected) {
                solar_monitor::Metrics::inc_active_connections();
                SNLOG_DEBUG("new connection: {}", conn->name());

                std::weak_ptr<solar_net::TcpConnection> weak_conn = conn;
                auto hs = std::make_shared<HttpConnContext>();
                hs->parser = std::make_shared<solar_http::HttpParser>(
                    [weak_conn, hs, router, static_handler, redis, rate_limiter](
                        solar_http::HttpRequest& req) {
                        auto conn = weak_conn.lock();
                        if (!conn) {
                            return;
                        }
                        // WebSocket Upgrade 优先处理（/ws/metrics、/ws/upload/{id}）
                        if (solar_ws::try_handle_upgrade(req, conn, redis)) {
                            return;
                        }

                        solar_http::HttpResponse resp;
                        const bool keep_alive = req.wants_keep_alive() &&
                                                hs->request_count < kMaxKeepAliveRequests;
                        hs->request_count++;

                        // GET 优先尝试静态资源（Web UI、share.html 等）
                        if (req.method() == solar_http::HttpMethod::GET &&
                            static_handler->try_serve(req, resp)) {
                            record_response_metrics(resp);
                            solar_http::send_response(conn, resp, keep_alive);
                            return;
                        }

                        // per-IP 滑动窗口限流（API / 分享 / metrics 等；静态资源豁免）
                        if (!solar_cache::RateLimiter::is_exempt(req.path())) {
                            const std::string client_ip = solar_cache::RateLimiter::resolve_client_ip(
                                req.get_header_ic("X-Forwarded-For"), conn->peer_ip());
                            if (!rate_limiter->allow(client_ip)) {
                                resp.set_status(429, "Too Many Requests");
                                resp.set_header("Retry-After",
                                                std::to_string(solar_cache::RateLimiter::kWindowSeconds));
                                resp.set_json(R"({"error":"Too many requests"})");
                                record_response_metrics(resp);
                                solar_http::send_response(conn, resp, keep_alive);
                                return;
                            }
                        }

                        // 非白名单 API 需 JWT 鉴权，结果写入 req 供 Handler 只读
                        if (!solar_auth::AuthMiddleware::is_whitelisted(req.path())) {
                            auto auth_result = solar_auth::AuthMiddleware::authenticate(req);
                            if (!auth_result.authenticated) {
                                resp.set_error(401, auth_result.error_msg);
                                record_response_metrics(resp);
                                solar_http::send_response(conn, resp, keep_alive);
                                return;
                            }
                            req.set_auth_user(auth_result.user_id, auth_result.username);
                        }

                        router->dispatch(req, resp);
                        record_response_metrics(resp);
                        solar_http::send_response(conn, resp, keep_alive);
                    }
                );
                conn->set_context(hs);
            } else if (conn->state() == solar_net::TcpConnection::State::kDisconnected) {
                solar_monitor::Metrics::dec_active_connections();
                solar_ws::WsSessionManager::instance().remove_session(conn);
                SNLOG_DEBUG("connection closed: {}", conn->name());
            }
        }
    );

    // 读事件：根据连接上下文分发给 HttpParser 或 WsHandler
    server.set_message_callback(
        [&](const solar_net::TcpConnectionPtr& conn,
             solar_net::Buffer* buf,
             int64_t) {
            const size_t readable = buf->readable_bytes();
            if (readable == 0) {
                return;
            }

            const auto& ctx = conn->get_context();
            if (ctx.type() == typeid(std::shared_ptr<solar_ws::WsHandler>)) {
                auto ws = std::any_cast<std::shared_ptr<solar_ws::WsHandler>>(ctx);
                const size_t consumed = ws->feed(
                    reinterpret_cast<const char*>(buf->data()), readable);
                buf->retrieve(consumed);
                return;
            }

            auto hs = std::any_cast<std::shared_ptr<HttpConnContext>>(ctx);
            const size_t consumed = hs->parser->feed(
                reinterpret_cast<const char*>(buf->data()), readable);
            buf->retrieve(consumed);
        }
    );

    // 每 5 秒向 /ws/metrics 订阅者推送指标（替代前端轮询）
    main_loop.run_every(5.0, []() {
        solar_ws::WsSessionManager::instance().broadcast_metrics();
    });

    // 每小时：撤销过期分享 + 回收孤儿 content 与磁盘块
    main_loop.run_every(3600.0, [&share_dao, &content_gc]() {
        const int expired = share_dao.revoke_expired();
        if (expired > 0) {
            SNLOG_INFO("revoked {} expired share(s)", expired);
        }
        content_gc.collect_orphans(200);
    });

    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    SNLOG_INFO("SolarDrive started");
    log_access_urls(cfg.server.port);

    server.start();
    main_loop.loop();

    SNLOG_INFO("SolarDrive stopped");
    solar_net::log::shutdown();
    return 0;
}
