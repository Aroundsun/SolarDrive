#pragma once

#include "../http/http_request.h"
#include "../http/http_response.h"
#include "../storage/object_store.h"
#include "../metadata/file_dao.h"

#include <nlohmann/json.hpp>

namespace solar_api {

// 上传 Handler
// 处理文件上传请求，支持秒传（通过 SHA-256 去重）和分块存储
class UploadHandler {
public:
    UploadHandler(solar_storage::ObjectStore& store,
                  solar_metadata::FileDao& dao)
        : store_(store), dao_(dao) {}

    void handle(const solar_http::HttpRequest& req,
                solar_http::HttpResponse& resp);

private:
    solar_storage::ObjectStore& store_;
    solar_metadata::FileDao&     dao_;
};

} // namespace solar_api
