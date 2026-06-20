#include "file_handler.h"
#include "../auth/request_auth.h"
#include "../auth/access_control.h"
#include "../http/query_utils.h"

#include <nlohmann/json.hpp>

namespace solar_api {

FileHandler::FileHandler(solar_metadata::FileDao& file_dao)
    : file_dao_(file_dao) {}

void FileHandler::handle_list(const solar_http::HttpRequest& req,
                              solar_http::HttpResponse& resp) {
    try {
        if (!solar_auth::require_auth_user(req, resp)) {
            return;
        }

        const std::string folder_id = solar_http::get_query_param(req, "folder_id");
        nlohmann::json files = nlohmann::json::array();
        for (const auto& f : file_dao_.list_by_folder(req.auth_user_id(), folder_id)) {
            files.push_back({
                {"id", f.id},
                {"name", f.name},
                {"size", f.size},
                {"hash", f.hash},
                {"mime_type", f.mime_type},
                {"created_at", f.created_at},
            });
        }
        resp.set_json(nlohmann::json({{"files", files}}).dump());
    } catch (const std::exception& e) {
        resp.set_error(500, std::string("list failed: ") + e.what());
    }
}

void FileHandler::handle_delete(const solar_http::HttpRequest& req,
                                solar_http::HttpResponse& resp) {
    try {
        if (!solar_auth::require_auth_user(req, resp)) {
            return;
        }

        const std::string file_id = req.get_path_param("id");
        if (file_id.empty()) {
            resp.set_error(400, "missing file id");
            return;
        }

        auto file = solar_auth::require_owned_file(
            req.auth_user_id(), file_id, file_dao_, resp);
        if (!file) {
            return;
        }

        file_dao_.soft_delete(file_id);
        resp.set_json(R"({"deleted":true})");
    } catch (const std::exception& e) {
        resp.set_error(500, std::string("delete failed: ") + e.what());
    }
}

} // namespace solar_api
