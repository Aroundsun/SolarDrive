#pragma once

// download_handler.h — 文件下载 API 处理器
// 按 file_id 查询元数据，从对象存储还原分块内容并返回二进制响应

#include "../http/http_request.h"
#include "../http/http_response.h"
#include "../storage/object_store.h"
#include "../metadata/file_dao.h"

#include <nlohmann/json.hpp>

namespace solar_api {

// 下载处理器：根据元数据中的 chunk_hashes 拼接完整文件
class DownloadHandler {
public:
    DownloadHandler(solar_storage::ObjectStore& store,
                    solar_metadata::FileDao& dao)
        : store_(store), dao_(dao) {}

    // GET /api/v1/files/{id} — 下载文件，设置 Content-Disposition
    void handle(const solar_http::HttpRequest& req,
                solar_http::HttpResponse& resp);

private:
    solar_storage::ObjectStore& store_;
    solar_metadata::FileDao&     dao_;
};

} // namespace solar_api
