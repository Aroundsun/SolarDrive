#include "download_handler.h"

#include <nlohmann/json.hpp>
#include <pqxx/except>

#include <stdexcept>
#include <vector>

using json = nlohmann::json;

namespace solar_api {

void DownloadHandler::handle(const solar_http::HttpRequest& req,
                             solar_http::HttpResponse& resp)
{
    try {
        // 1. 从 path_params 或 URL 路径中提取 file_id
        std::string file_id = req.get_path_param("id");
        if (file_id.empty()) {
            // 尝试从 query string 中获取
            const std::string& query = req.query();
            if (!query.empty()) {
                // 简单解析 query: key=value&key2=value2
                std::string key = "file_id=";
                size_t pos = query.find(key);
                if (pos != std::string::npos) {
                    pos += key.size();
                    size_t end = query.find('&', pos);
                    if (end == std::string::npos) {
                        file_id = query.substr(pos);
                    } else {
                        file_id = query.substr(pos, end - pos);
                    }
                }
            }
        }

        if (file_id.empty()) {
            resp.set_error(400, "file_id is required");
            return;
        }

        // 2. 通过 dao_.find_by_id() 查询，不存在返回 404
        auto record = dao_.find_by_id(file_id);
        if (!record.has_value()) {
            resp.set_error(404, "file not found");
            return;
        }

        // 3. 解析 chunk_hashes（JSON 数组）
        json chunk_json = json::parse(record->chunk_hashes);
        std::vector<std::string> chunk_hashes;
        for (const auto& h : chunk_json) {
            chunk_hashes.push_back(h.get<std::string>());
        }

        // 4. 调用 store_.get_chunked() 还原文件内容
        std::string content = store_.get_chunked(chunk_hashes);

        // 5. 设置 Content-Disposition header 和正确的 Content-Type
        std::string filename = record->name;
        std::string mime_type = record->mime_type;
        if (mime_type.empty()) {
            mime_type = "application/octet-stream";
        }

        // 6. 调用 resp.set_binary() 返回文件内容
        resp.set_binary(content, mime_type);
        resp.set_download_headers(filename, content.size());

    } catch (const pqxx::failure& e) {
        resp.set_error(500, "database error");
    } catch (const std::exception& e) {
        resp.set_error(500, e.what());
    }
}

} // namespace solar_api
