#include "folder_handler.h"
#include "../http/query_utils.h"
#include "../auth/access_control.h"
#include "../auth/request_auth.h"

#include <nlohmann/json.hpp>

namespace solar_api {

using json = nlohmann::json;

namespace {

json optional_id_json(const std::string& id) {
    if (id.empty()) {
        return json(nullptr);
    }
    return id;
}

} // namespace

FolderHandler::FolderHandler(solar_metadata::FolderDao& folder_dao,
                             solar_metadata::FileDao& file_dao)
    : folder_dao_(folder_dao), file_dao_(file_dao) {}

void FolderHandler::handle_list(const solar_http::HttpRequest& req,
                                solar_http::HttpResponse& resp) {
    try {
        if (!solar_auth::require_auth_user(req, resp)) {
            return;
        }
        const std::string& user_id = req.auth_user_id();

        const std::string parent_id = solar_http::get_query_param(req, "parent_id");

        json folders = json::array();
        for (const auto& folder : folder_dao_.list_by_parent(user_id, parent_id)) {
            folders.push_back({
                {"id", folder.id},
                {"name", folder.name},
                {"parent_id", optional_id_json(folder.parent_id)},
                {"created_at", folder.created_at},
            });
        }

        resp.set_json(json({{"folders", folders}}).dump());
    } catch (const std::exception& e) {
        resp.set_error(500, std::string("list folders failed: ") + e.what());
    }
}

void FolderHandler::handle_create(const solar_http::HttpRequest& req,
                                  solar_http::HttpResponse& resp) {
    try {
        if (!solar_auth::require_auth_user(req, resp)) {
            return;
        }
        const std::string& user_id = req.auth_user_id();

        json body = json::parse(req.body());
        std::string name = body.value("name", "");
        if (name.empty()) {
            resp.set_error(400, "name is required");
            return;
        }

        std::string parent_id;
        if (body.contains("parent_id") && !body["parent_id"].is_null()) {
            parent_id = body["parent_id"].get<std::string>();
        }

        if (!parent_id.empty()) {
            auto parent = folder_dao_.find_by_id(parent_id);
            if (!parent || parent->owner_id != user_id) {
                resp.set_error(404, "parent folder not found");
                return;
            }
        }

        if (folder_dao_.name_exists(user_id, parent_id, name)) {
            resp.set_error(409, "folder name already exists");
            return;
        }

        solar_metadata::FolderRecord folder;
        folder.name = name;
        folder.parent_id = parent_id;
        folder.owner_id = user_id;

        const std::string id = folder_dao_.insert(folder);
        resp.set_json(json({
            {"id", id},
            {"name", name},
            {"parent_id", optional_id_json(parent_id)},
        }).dump());
    } catch (const json::exception& e) {
        resp.set_error(400, std::string("invalid JSON: ") + e.what());
    } catch (const std::exception& e) {
        resp.set_error(500, std::string("create folder failed: ") + e.what());
    }
}

void FolderHandler::handle_delete(const solar_http::HttpRequest& req,
                                  solar_http::HttpResponse& resp) {
    try {
        if (!solar_auth::require_auth_user(req, resp)) {
            return;
        }
        const std::string& user_id = req.auth_user_id();

        const std::string id = req.get_path_param("id");
        if (id.empty()) {
            resp.set_error(400, "missing folder id");
            return;
        }

        auto folder = folder_dao_.find_by_id(id);
        if (!folder || folder->owner_id != user_id) {
            resp.set_error(404, "folder not found");
            return;
        }

        if (folder_dao_.has_children(id)) {
            resp.set_error(400, "folder is not empty");
            return;
        }

        folder_dao_.soft_delete(id);
        resp.set_json(R"({"deleted":true})");
    } catch (const std::exception& e) {
        resp.set_error(500, std::string("delete folder failed: ") + e.what());
    }
}

void FolderHandler::handle_rename(const solar_http::HttpRequest& req,
                                  solar_http::HttpResponse& resp) {
    try {
        if (!solar_auth::require_auth_user(req, resp)) {
            return;
        }
        const std::string& user_id = req.auth_user_id();

        const std::string id = req.get_path_param("id");
        if (id.empty()) {
            resp.set_error(400, "missing folder id");
            return;
        }

        auto folder = folder_dao_.find_by_id(id);
        if (!folder || folder->owner_id != user_id) {
            resp.set_error(404, "folder not found");
            return;
        }

        json body = json::parse(req.body());
        std::string name = body.value("name", "");
        if (name.empty()) {
            resp.set_error(400, "name is required");
            return;
        }

        if (folder_dao_.name_exists(user_id, folder->parent_id, name) &&
            name != folder->name) {
            resp.set_error(409, "folder name already exists");
            return;
        }

        folder_dao_.rename(id, name);
        resp.set_json(json({{"id", id}, {"name", name}}).dump());
    } catch (const json::exception& e) {
        resp.set_error(400, std::string("invalid JSON: ") + e.what());
    } catch (const std::exception& e) {
        resp.set_error(500, std::string("rename folder failed: ") + e.what());
    }
}

} // namespace solar_api
