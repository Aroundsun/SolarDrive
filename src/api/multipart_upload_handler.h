#pragma once

#include "../http/http_request.h"
#include "../http/http_response.h"
#include "../storage/object_store.h"
#include "../metadata/file_dao.h"
#include "../cache/redis_client.h"
#include <memory>
#include <string>
#include <vector>

namespace solar_api {

// 分片上传会话状态
struct MultipartSession {
    std::string upload_id;
    std::string user_id;
    std::string file_name;
    std::string mime_type;
    int64_t     total_size;
    int         chunk_count;              // 总片数
    std::vector<bool> uploaded;          // 是否已上传
    std::vector<std::string> part_hashes; // 每个分片的 hash
    int64_t     chunk_size;
};

class MultipartUploadHandler {
public:
    MultipartUploadHandler(
        solar_storage::ObjectStore& store,
        solar_metadata::FileDao& dao,
        std::shared_ptr<solar_cache::RedisClient> redis);

    // POST /api/v1/upload/init
    // Body: {"file_name":"...","total_size":...}
    void handle_init(const solar_http::HttpRequest& req, solar_http::HttpResponse& resp);

    // PUT /api/v1/upload/{upload_id}/part/{part_num}
    // Body: 分片二进制数据
    void handle_upload_part(const solar_http::HttpRequest& req, solar_http::HttpResponse& resp);

    // POST /api/v1/upload/{upload_id}/complete
    void handle_complete(const solar_http::HttpRequest& req, solar_http::HttpResponse& resp);

    // GET /api/v1/upload/{upload_id}
    // 查询上传状态（断点恢复用）
    void handle_status(const solar_http::HttpRequest& req, solar_http::HttpResponse& resp);

    // DELETE /api/v1/upload/{upload_id}
    void handle_abort(const solar_http::HttpRequest& req, solar_http::HttpResponse& resp);

    // 在 upload handler 中增加秒传 Redis 优先查询
    static void handle_upload_with_redis(
        const solar_http::HttpRequest& req,
        solar_http::HttpResponse& resp,
        solar_storage::ObjectStore& store,
        solar_metadata::FileDao& dao,
        std::shared_ptr<solar_cache::RedisClient> redis);

private:
    MultipartSession load_from_redis(const std::string& upload_id);
    void save_to_redis(const MultipartSession& session);
    void delete_from_redis(const std::string& upload_id);

    solar_storage::ObjectStore& store_;
    solar_metadata::FileDao& dao_;
    std::shared_ptr<solar_cache::RedisClient> redis_;
};

} // namespace solar_api
