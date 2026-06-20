#pragma once

#include "../http/http_request.h"
#include "../network/tcp_connection.h"
#include "../cache/redis_client.h"

#include <memory>

namespace solar_ws {

// 处理 WebSocket HTTP Upgrade 请求
// 成功时发送 101 并将连接上下文切换为 WsHandler，返回 true
bool try_handle_upgrade(const solar_http::HttpRequest& req,
                        const solar_net::TcpConnectionPtr& conn,
                        const std::shared_ptr<solar_cache::RedisClient>& redis);

} // namespace solar_ws
