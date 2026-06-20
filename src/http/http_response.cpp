#include "http_response.h"

#include "../network/tcp_connection.h"

#include <nlohmann/json.hpp>

namespace solar_http {

void HttpResponse::set_error(int code, const std::string& msg) {
    status_code_ = code;
    status_text_ = msg;
    set_json(nlohmann::json{{"error", msg}}.dump());
}

void send_response(const std::shared_ptr<solar_net::TcpConnection>& conn,
                   HttpResponse& resp,
                   bool keep_alive) {
    resp.set_close_connection(!keep_alive);
    const std::string payload = resp.serialize();

    if (keep_alive) {
        conn->set_write_complete_callback(
            [](const std::shared_ptr<solar_net::TcpConnection>&) {});
    } else {
        conn->set_write_complete_callback(
            [conn](const std::shared_ptr<solar_net::TcpConnection>&) {
                conn->force_close();
            });
    }
    conn->send(payload);
}

void send_response_and_close(const std::shared_ptr<solar_net::TcpConnection>& conn,
                             HttpResponse& resp) {
    send_response(conn, resp, false);
}

} // namespace solar_http
