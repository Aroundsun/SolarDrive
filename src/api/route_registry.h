#pragma once

#include "../http/http_router.h"
#include "../storage/object_store.h"
#include "../metadata/file_dao.h"
#include "../metadata/folder_dao.h"
#include "../config/config.h"
#include "../cache/redis_client.h"

#include <memory>

namespace solar_api {

class AuthHandler;
class DownloadHandler;
class MultipartUploadHandler;
class ShareHandler;
class FolderHandler;
class FileHandler;

struct AppServices {
    const solar_config::AppConfig& cfg;
    solar_storage::ObjectStore& store;
    solar_metadata::FileDao& file_dao;
    solar_metadata::FolderDao& folder_dao;
    std::shared_ptr<solar_cache::RedisClient> redis;

    AuthHandler& auth;
    DownloadHandler& download;
    MultipartUploadHandler& multipart;
    ShareHandler& share;
    FolderHandler& folder;
    FileHandler& files;
};

void register_api_routes(const std::shared_ptr<solar_http::HttpRouter>& router,
                         const AppServices& svc);

} // namespace solar_api
