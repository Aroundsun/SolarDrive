#pragma once

#include "../http/http_request.h"
#include "../http/http_response.h"
#include "../storage/object_store.h"
#include "../metadata/file_dao.h"

#include <nlohmann/json.hpp>

namespace solar_api {

// 下载 Handler
// 通过 file_id 查询元数据，还原分块文件内容并返回
class DownloadHandler {
public:
    DownloadHandler(solar_storage::ObjectStore& store,
                    solar_metadata::FileDao& dao)
        : store_(store), dao_(dao) {}

    void handle(const solar_http::HttpRequest& req,
                solar_http::HttpResponse& resp);

private:
    solar_storage::ObjectStore& store_;
    solar_metadata::FileDao&     dao_;
};

} // namespace solar_api
