#include <memory>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <csignal>

#include "network/tcp_server.h"
#include "network/event_loop.h"
#include "network/tcp_connection.h"
#include "network/socket.h"
#include "http/http_request.h"
#include "http/http_response.h"
#include "http/http_parser.h"
#include "http/http_router.h"
#include "storage/object_store.h"
#include "metadata/db_pool.h"
#include "metadata/file_dao.h"
#include "api/upload_handler.h"
#include "api/download_handler.h"

#include <pqxx/except>

// 全局指针，用于信号处理
solar_net::EventLoop* g_main_loop = nullptr;
solar_net::TcpServer* g_server   = nullptr;

void signal_handler(int) {
    std::cout << "\n[main] received SIGINT, shutting down...\n";
    if (g_server)   g_server->stop();
    if (g_main_loop) g_main_loop->stop();
}

int main(int argc, char* argv[]) {
    // ---- 命令行参数解析 ----
    uint16_t port = 8080;
    if (argc >= 2) port = static_cast<uint16_t>(std::atoi(argv[1]));

    const char* db_conn_str = std::getenv("SOLAR_DB");
    if (!db_conn_str) {
        db_conn_str = "host=127.0.0.1 port=5432 dbname=solardrive user=postgres password=postgres";
    }

    const char* store_path = std::getenv("SOLAR_STORE");
    if (!store_path) {
        store_path = "/tmp/solardrive/objects";
    }

    // 存储与数据库（先于网络栈初始化，连接失败时尽早退出）
    solar_storage::ObjectStore store(store_path);

    std::unique_ptr<solar_metadata::DbPool> db_pool;
    try {
        pqxx::connection probe(db_conn_str);
        db_pool = std::make_unique<solar_metadata::DbPool>(db_conn_str, 8);
    } catch (const pqxx::failure& e) {
        std::cerr << "[main] ERROR: cannot connect to PostgreSQL: " << e.what() << "\n"
                  << "[main] Check that PostgreSQL is running and SOLAR_DB is correct.\n"
                  << "[main] Example: export SOLAR_DB=\"host=127.0.0.1 port=5432 "
                  << "dbname=solardrive user=postgres password=postgres\"\n";
        std::_Exit(1);
    }

    solar_metadata::FileDao file_dao(*db_pool);

    try {
        file_dao.create_table();
        std::cout << "[main] database table ready.\n";
    } catch (const std::exception& e) {
        std::cerr << "[main] WARN: create_table: " << e.what()
                  << " (may already exist)\n";
    }

    // API Handlers
    solar_api::UploadHandler   upload_handler(store, file_dao);
    solar_api::DownloadHandler download_handler(store, file_dao);

    // ---- 网络栈 ----
    solar_net::EventLoop main_loop;
    g_main_loop = &main_loop;

    ::sockaddr_in listen_addr;
    std::memset(&listen_addr, 0, sizeof(listen_addr));
    listen_addr.sin_family      = AF_INET;
    listen_addr.sin_addr.s_addr = INADDR_ANY;
    listen_addr.sin_port        = ::htons(port);

    solar_net::TcpServer server(&main_loop, listen_addr, "SolarDrive");
    server.set_thread_num(4);
    g_server = &server;

    // 路由器（shared_ptr，供 connection_callback 和 message_callback 共用）
    auto router = std::make_shared<solar_http::HttpRouter>();

    router->get("/api/v1/health", [](const solar_http::HttpRequest&,
                                       solar_http::HttpResponse& resp) {
        resp.set_json(R"({"status":"ok","service":"SolarDrive"})");
    });

    router->post("/api/v1/upload", [&](const solar_http::HttpRequest& req,
                                          solar_http::HttpResponse& resp) {
        try {
            upload_handler.handle(req, resp);
        } catch (const std::exception& e) {
            resp.set_error(500, std::string("upload failed: ") + e.what());
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

    // ---- 连接建立时：给每条连接挂一个 HttpParser ----
    server.set_connection_callback(
        [router](const solar_net::TcpConnectionPtr& conn) {
            if (conn->state() == solar_net::TcpConnection::State::kConnected) {
                std::cout << "[main] new connection: " << conn->name() << "\n";
                // 每条连接创建独立的 HttpParser
                // callback：解析出一个完整请求后，调用路由
                auto parser = std::make_shared<solar_http::HttpParser>(
                    [conn, router](const solar_http::HttpRequest& req) {
                        // 注意：此 callback 在 feed() 的调用栈内执行
                        // 需要把请求数据从 parser 里取出来
                        // 设计调整：让 HttpParser 把解析好的请求通过 callback 传出来
                        // （当前 HttpParser 的 callback 签名是 void(const HttpRequest&)，OK）
                        solar_http::HttpResponse resp;
                        router->dispatch(req, resp);
                        std::string resp_str = resp.serialize();
                        conn->send(resp_str);
                    }
                );
                conn->set_context(parser);
            } else if (conn->state() == solar_net::TcpConnection::State::kDisconnected) {
                std::cout << "[main] connection closed: " << conn->name() << "\n";
            }
        }
    );

    // ---- 收到数据时：喂给该连接的 HttpParser ----
    server.set_message_callback(
        [&](const solar_net::TcpConnectionPtr& conn,
             solar_net::Buffer* buf,
             int64_t) {
            // 取出该连接的 HttpParser
            auto parser = std::any_cast<std::shared_ptr<solar_http::HttpParser>>(
                conn->get_context()
            );

            // 从 Buffer 取数据喂给 parser
            size_t readable = buf->readable_bytes();
            if (readable == 0) return;

            // 使用 feed() 批量喂数据
            size_t consumed = parser->feed(reinterpret_cast<const char*>(buf->data()), readable);
            buf->retrieve(consumed);
        }
    );

    // ---- 信号处理 ----
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "[SolarDrive] listening on port " << port << "\n";
    std::cout << "[SolarDrive] storage path: " << store_path << "\n";

    server.start();
    main_loop.loop();

    std::cout << "[SolarDrive] server stopped.\n";
    return 0;
}
