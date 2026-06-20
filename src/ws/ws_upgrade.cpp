#include "ws_upgrade.h"
#include "websocket.h"
#include "ws_handler.h"
#include "ws_session.h"
#include "../auth/auth_middleware.h"
#include "../auth/jwt.h"
#include "../http/query_utils.h"

#include "../cache/redis_client.h"

#include <algorithm>
#include <cctype>
#include <optional>

namespace solar_ws {

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool is_websocket_upgrade(const solar_http::HttpRequest& req) {
    const std::string upgrade = to_lower(req.get_header("Upgrade"));
    if (upgrade != "websocket") {
        return false;
    }
    const std::string connection = to_lower(req.get_header("Connection"));
    return connection.find("upgrade") != std::string::npos;
}

std::string path_after_prefix(const std::string& path, const std::string& prefix) {
    if (path.size() <= prefix.size() || path.compare(0, prefix.size(), prefix) != 0) {
        return "";
    }
    return path.substr(prefix.size());
}

void send_http_error(const solar_net::TcpConnectionPtr& conn, int code, const std::string& msg) {
    solar_http::HttpResponse resp;
    resp.set_error(code, msg);
    conn->send(resp.serialize());
}

std::optional<std::string> authenticate_ws(const solar_http::HttpRequest& req,
                                           solar_http::HttpResponse& resp) {
    solar_http::HttpRequest auth_req = req;
    std::string token = solar_http::query_param(req.query(), "token");
    if (token.empty()) {
        token = solar_auth::JwtUtil::extract_token(req.get_header("Authorization")).value_or("");
    }
    if (!token.empty()) {
        auth_req.add_header("Authorization", "Bearer " + token);
    }

    auto auth = solar_auth::AuthMiddleware::authenticate(auth_req);
    if (!auth.authenticated) {
        resp.set_error(401, auth.error_msg);
        return std::nullopt;
    }
    return auth.user_id;
}

bool upload_session_owned_by(const std::shared_ptr<solar_cache::RedisClient>& redis,
                             const std::string& upload_id,
                             const std::string& user_id) {
    if (!redis) {
        return false;
    }
    auto owner = redis->hget("multipart:" + upload_id, "user_id");
    return owner && *owner == user_id;
}

} // namespace

bool try_handle_upgrade(const solar_http::HttpRequest& req,
                        const solar_net::TcpConnectionPtr& conn,
                        const std::shared_ptr<solar_cache::RedisClient>& redis) {
    if (req.method() != solar_http::HttpMethod::GET || !is_websocket_upgrade(req)) {
        return false;
    }

    const std::string ws_key = req.get_header("Sec-WebSocket-Key");
    if (ws_key.empty()) {
        return false;
    }

    const std::string path = req.path();

    // GET /ws/metrics — 监控推送（免鉴权，替代轮询）
    if (path == "/ws/metrics") {
        const std::string upgrade_resp = WsHandshake::build_upgrade_response(ws_key);
        conn->send(upgrade_resp);

        auto ws_handler = std::make_shared<WsHandler>(conn, "");
        WsSessionManager::instance().register_metrics(conn);
        conn->set_context(ws_handler);
        WsSessionManager::instance().push_metrics_snapshot(conn);
        return true;
    }

    // GET /ws/upload/{upload_id} — 分片上传进度推送（需鉴权）
    const std::string upload_prefix = "/ws/upload/";
    if (path.compare(0, upload_prefix.size(), upload_prefix) == 0) {
        const std::string upload_id = path_after_prefix(path, upload_prefix);
        if (upload_id.empty()) {
            send_http_error(conn, 400, "missing upload_id");
            return true;
        }

        solar_http::HttpResponse auth_resp;
        auto user_id = authenticate_ws(req, auth_resp);
        if (!user_id) {
            conn->send(auth_resp.serialize());
            return true;
        }
        if (!upload_session_owned_by(redis, upload_id, *user_id)) {
            send_http_error(conn, 403, "forbidden");
            return true;
        }

        const std::string upgrade_resp = WsHandshake::build_upgrade_response(ws_key);
        conn->send(upgrade_resp);

        auto ws_handler = std::make_shared<WsHandler>(conn, upload_id);
        WsSessionManager::instance().register_session(upload_id, conn);
        conn->set_context(ws_handler);
        return true;
    }

    return false;
}

} // namespace solar_ws
