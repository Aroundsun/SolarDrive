#pragma once

#include "../http/http_request.h"
#include "../http/http_response.h"
#include "../storage/object_store.h"
#include "../metadata/file_dao.h"
#include "../metadata/folder_dao.h"
#include "../cache/redis_client.h"
#include <memory>
#include <string>
#include <vector>

namespace solar_api {

struct MultipartSession {
    std::string upload_id;
    std::string user_id;
    std::string folder_id;
    std::string file_name;
    std::string mime_type;
    int64_t     total_size = 0;
    int64_t     chunk_size = 0;
    int         chunk_count = 0;
    std::vector<bool> uploaded;
    std::vector<std::string> part_hashes;
};

class MultipartUploadHandler {
public:
    MultipartUploadHandler(
        solar_storage::ObjectStore& store,
        solar_metadata::FileDao& dao,
        solar_metadata::FolderDao& folder_dao,
        std::shared_ptr<solar_cache::RedisClient> redis,
        size_t chunk_size,
        int64_t max_file_size_bytes);

    void handle_init(const solar_http::HttpRequest& req, solar_http::HttpResponse& resp);
    void handle_upload_part(const solar_http::HttpRequest& req, solar_http::HttpResponse& resp);
    void handle_complete(const solar_http::HttpRequest& req, solar_http::HttpResponse& resp);
    void handle_status(const solar_http::HttpRequest& req, solar_http::HttpResponse& resp);
    void handle_abort(const solar_http::HttpRequest& req, solar_http::HttpResponse& resp);

private:
    MultipartSession load_from_redis(const std::string& upload_id);
    void save_to_redis(const MultipartSession& session);
    void delete_from_redis(const std::string& upload_id);

    solar_storage::ObjectStore& store_;
    solar_metadata::FileDao& dao_;
    solar_metadata::FolderDao& folder_dao_;
    std::shared_ptr<solar_cache::RedisClient> redis_;
    size_t chunk_size_;
    int64_t max_file_size_bytes_;
};

} // namespace solar_api
