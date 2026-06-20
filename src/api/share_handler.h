#pragma once

// share_handler.h — 文件分享 API 处理器
// 创建/撤销分享链接，公开下载与转存到个人网盘，支持密码与过期限制

#include "../http/http_request.h"
#include "../http/http_response.h"
#include "../storage/object_store.h"
#include "../metadata/file_dao.h"
#include "../metadata/share_dao.h"
#include "../cache/redis_client.h"
#include <optional>

namespace solar_api {

// 分享处理器：管理分享 token 生命周期与访问校验
class ShareHandler {
public:
    ShareHandler(
        solar_metadata::ShareDao& share_dao,
        solar_metadata::FileDao& file_dao,
        solar_storage::ObjectStore& store,
        std::shared_ptr<solar_cache::RedisClient> redis);

    // POST /api/v1/share — 创建分享（需鉴权）
    void handle_create(const solar_http::HttpRequest& req, solar_http::HttpResponse& resp);

    // GET /api/v1/shares — 我的分享列表（需鉴权）
    void handle_list(const solar_http::HttpRequest& req, solar_http::HttpResponse& resp);

    // DELETE /api/v1/share/{token} — 撤销分享（需鉴权）
    void handle_revoke(const solar_http::HttpRequest& req, solar_http::HttpResponse& resp);

    // GET /s/{token} — 分享下载（公开，无需鉴权）
    void handle_download(const solar_http::HttpRequest& req, solar_http::HttpResponse& resp);

    // POST /api/v1/share/save — 保存分享文件到网盘（需鉴权）
    void handle_save(const solar_http::HttpRequest& req, solar_http::HttpResponse& resp);

private:
    solar_metadata::ShareDao& share_dao_;
    solar_metadata::FileDao&  file_dao_;
    solar_storage::ObjectStore& store_;
    std::shared_ptr<solar_cache::RedisClient> redis_;

    // 生成 8 位随机分享 token，冲突时重试
    std::string generate_token();

    // 校验分享有效性（撤销/过期/密码/下载次数），count_download 为 true 时递增计数
    std::optional<solar_metadata::ShareRecord> resolve_share(
        const std::string& token,
        const std::string& password,
        solar_http::HttpResponse& resp,
        bool count_download);
};

} // namespace solar_api
