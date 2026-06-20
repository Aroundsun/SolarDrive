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
#include "metadata/file_dao.h"
#include "api/upload_handler.h"
#include "api/download_handler.h"
#include "api/auth_handler.h"
#include "api/multipart_upload_handler.h"
#include "auth/jwt.h"
#include "auth/auth_middleware.h"
#include "auth/user_dao.h"
#include "cache/redis_client.h"
#include "config/config.h"
#include "monitor/metrics.h"

#include <pqxx/except>
#include <nlohmann/json.hpp>

namespace {

bool web_root_has_index(const std::string& root) {
    return std::ifstream(root + "/index.html").good();
}

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
    bool config_explicit = false;
    std::optional<uint16_t> port_override;
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

solar_net::EventLoop* g_main_loop = nullptr;
solar_net::TcpServer* g_server   = nullptr;

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
    SNLOG_INFO("database: {}:{}/{}", cfg.database.host, cfg.database.port, cfg.database.dbname);
    SNLOG_INFO("redis: {}:{}", cfg.redis.host, cfg.redis.port);
    SNLOG_INFO("web root: {}", web_dir);

    solar_storage::ObjectStore store(cfg.storage.base_path);

    std::unique_ptr<solar_metadata::DbPool> db_pool;
    try {
        pqxx::connection probe(db_conn);
        db_pool = std::make_unique<solar_metadata::DbPool>(db_conn, cfg.database.pool_size);
    } catch (const pqxx::failure& e) {
        SNLOG_CRITICAL("cannot connect to PostgreSQL: {}", e.what());
        solar_net::log::shutdown();
        std::_Exit(1);
    }

    solar_metadata::FileDao file_dao(*db_pool);

    try {
        file_dao.create_table();
        SNLOG_INFO("database tables ready");
    } catch (const std::exception& e) {
        SNLOG_WARN("create_table: {}", e.what());
    }

    auto redis = std::make_shared<solar_cache::RedisClient>(
        cfg.redis.host, cfg.redis.port);
    if (redis->ping()) {
        SNLOG_INFO("Redis connected");
    } else {
        SNLOG_WARN("Redis not available, continuing without cache");
    }

    solar_auth::JwtUtil::set_secret(cfg.jwt.secret);

    auto user_dao = std::make_shared<solar_auth::UserDao>(*db_pool);
    try {
        user_dao->create_table();
    } catch (const std::exception& e) {
        SNLOG_WARN("create users table: {}", e.what());
    }

    solar_api::AuthHandler auth_handler(user_dao);
    solar_api::UploadHandler   upload_handler(store, file_dao);
    solar_api::DownloadHandler download_handler(store, file_dao);
    solar_api::MultipartUploadHandler multipart_handler(store, file_dao, redis);

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

    router->get("/api/v1/health", [](const solar_http::HttpRequest&,
                                       solar_http::HttpResponse& resp) {
        resp.set_json(R"({"status":"ok","service":"SolarDrive"})");
    });

    router->get("/metrics", [](const solar_http::HttpRequest&,
                                 solar_http::HttpResponse& resp) {
        resp.set_body(solar_monitor::Metrics::dump());
        resp.set_header("Content-Type", "text/plain; version=0.0.4; charset=utf-8");
    });

    router->post("/api/v1/auth/register", [&](const solar_http::HttpRequest& req,
                                                solar_http::HttpResponse& resp) {
        auth_handler.handle_register(req, resp);
    });

    router->post("/api/v1/auth/login", [&](const solar_http::HttpRequest& req,
                                             solar_http::HttpResponse& resp) {
        auth_handler.handle_login(req, resp);
    });

    router->post("/api/v1/upload", [&](const solar_http::HttpRequest& req,
                                        solar_http::HttpResponse& resp) {
        try {
            solar_api::MultipartUploadHandler::handle_upload_with_redis(
                req, resp, store, file_dao, redis);
        } catch (const std::exception& e) {
            resp.set_error(500, std::string("upload failed: ") + e.what());
        }
    });

    router->post("/api/v1/upload/init", [&](const solar_http::HttpRequest& req,
                                              solar_http::HttpResponse& resp) {
        multipart_handler.handle_init(req, resp);
    });

    router->put("/api/v1/upload/{upload_id}/part/{part_num}",
        [&](const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
            multipart_handler.handle_upload_part(req, resp);
        });

    router->post("/api/v1/upload/{upload_id}/complete",
        [&](const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
            multipart_handler.handle_complete(req, resp);
        });

    router->get("/api/v1/upload/{upload_id}",
        [&](const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
            multipart_handler.handle_status(req, resp);
        });

    router->del("/api/v1/upload/{upload_id}",
        [&](const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
            multipart_handler.handle_abort(req, resp);
        });

    router->get("/api/v1/files", [&](const solar_http::HttpRequest& req,
                                      solar_http::HttpResponse& resp) {
        (void)req;
        try {
            nlohmann::json files = nlohmann::json::array();
            for (const auto& f : file_dao.list_active()) {
                files.push_back({
                    {"id", f.id},
                    {"name", f.name},
                    {"size", f.size},
                    {"hash", f.hash},
                    {"mime_type", f.mime_type},
                    {"created_at", f.created_at},
                });
            }
            resp.set_json(nlohmann::json({{"files", files}}).dump());
        } catch (const std::exception& e) {
            resp.set_error(500, std::string("list failed: ") + e.what());
        }
    });

    router->get("/api/v1/files/{id}", [&](const solar_http::HttpRequest& req,
                                           solar_http::HttpResponse& resp) {
        try {
            download_handler.handle(req, resp);
        } catch (const std::exception& e) {
            resp.set_error(500, std::string("download failed: ") + e.what());
        }
    });

    router->del("/api/v1/files/{id}", [&](const solar_http::HttpRequest& req,
                                           solar_http::HttpResponse& resp) {
        try {
            auto it = req.path_params().find("id");
            if (it == req.path_params().end()) {
                resp.set_error(400, "missing file id");
                return;
            }
            file_dao.soft_delete(it->second);
            resp.set_json(R"({"deleted":true})");
        } catch (const std::exception& e) {
            resp.set_error(500, std::string("delete failed: ") + e.what());
        }
    });

    server.set_connection_callback(
        [router, static_handler](const solar_net::TcpConnectionPtr& conn) {
            if (conn->state() == solar_net::TcpConnection::State::kConnected) {
                solar_monitor::Metrics::inc_active_connections();
                SNLOG_DEBUG("new connection: {}", conn->name());

                auto parser = std::make_shared<solar_http::HttpParser>(
                    [conn, router, static_handler](const solar_http::HttpRequest& req) {
                        solar_http::HttpResponse resp;

                        if (req.method() == solar_http::HttpMethod::GET &&
                            static_handler->try_serve(req, resp)) {
                            record_response_metrics(resp);
                            conn->send(resp.serialize());
                            return;
                        }

                        if (!solar_auth::AuthMiddleware::is_whitelisted(req.path())) {
                            auto auth_result = solar_auth::AuthMiddleware::authenticate(req);
                            if (!auth_result.authenticated) {
                                resp.set_error(401, auth_result.error_msg);
                                record_response_metrics(resp);
                                conn->send(resp.serialize());
                                return;
                            }
                        }

                        router->dispatch(req, resp);
                        record_response_metrics(resp);
                        conn->send(resp.serialize());
                    }
                );
                conn->set_context(parser);
            } else if (conn->state() == solar_net::TcpConnection::State::kDisconnected) {
                solar_monitor::Metrics::dec_active_connections();
                SNLOG_DEBUG("connection closed: {}", conn->name());
            }
        }
    );

    server.set_message_callback(
        [&](const solar_net::TcpConnectionPtr& conn,
             solar_net::Buffer* buf,
             int64_t) {
            auto parser = std::any_cast<std::shared_ptr<solar_http::HttpParser>>(
                conn->get_context()
            );
            const size_t readable = buf->readable_bytes();
            if (readable == 0) return;
            const size_t consumed = parser->feed(
                reinterpret_cast<const char*>(buf->data()), readable);
            buf->retrieve(consumed);
        }
    );

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
