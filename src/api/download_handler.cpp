// download_handler.cpp — 文件下载 API 实现

#include "download_handler.h"
#include "../auth/access_control.h"
#include "../http/query_utils.h"

#include <nlohmann/json.hpp>
#include <pqxx/except>
#include <stdexcept>
#include <vector>

using json = nlohmann::json;

namespace solar_api {

void DownloadHandler::handle(const solar_http::HttpRequest& req,
                             solar_http::HttpResponse& resp) {
    try {
        if (!req.has_auth_user()) {
            resp.set_error(401, "Unauthorized");
            return;
        }

        std::string file_id = req.get_path_param("id");
        if (file_id.empty()) {
            file_id = solar_http::get_query_param(req, "file_id");
        }
        if (file_id.empty()) {
            resp.set_error(400, "file_id is required");
            return;
        }

        auto record = solar_auth::require_owned_file(
            req.auth_user_id(), file_id, dao_, resp);
        if (!record) {
            return;
        }

        json chunk_json = json::parse(record->chunk_hashes);
        std::vector<std::string> chunk_hashes;
        for (const auto& h : chunk_json) {
            chunk_hashes.push_back(h.get<std::string>());
        }

        std::string content = store_.get_chunked(chunk_hashes);
        std::string mime_type = record->mime_type.empty()
            ? "application/octet-stream"
            : record->mime_type;

        resp.set_binary(content, mime_type);
        resp.set_download_headers(record->name, content.size());

    } catch (const pqxx::failure&) {
        resp.set_error(500, "database error");
    } catch (const std::exception& e) {
        resp.set_error(500, e.what());
    }
}

} // namespace solar_api
