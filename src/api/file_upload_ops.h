#pragma once

#include "../http/http_request.h"
#include "../http/http_response.h"
#include "../metadata/file_dao.h"
#include "../metadata/folder_dao.h"
#include "../storage/object_store.h"
#include "../cache/redis_client.h"
#include <memory>
#include <string>

namespace solar_api {

// 单次上传（POST /api/v1/upload）：秒传 / 新写入 / Redis 缓存
void handle_single_upload(
    const solar_http::HttpRequest& req,
    solar_http::HttpResponse& resp,
    const std::string& user_id,
    solar_storage::ObjectStore& store,
    solar_metadata::FileDao& file_dao,
    solar_metadata::FolderDao& folder_dao,
    const std::shared_ptr<solar_cache::RedisClient>& redis,
    int64_t max_file_size_bytes);

// 秒传：为当前用户创建指向已有内容的文件项
std::string link_existing_content(
    solar_metadata::FileDao& file_dao,
    const solar_metadata::FileRecord& content,
    const std::string& filename,
    const std::string& mime_type,
    const std::string& user_id,
    const std::string& folder_id);

} // namespace solar_api
