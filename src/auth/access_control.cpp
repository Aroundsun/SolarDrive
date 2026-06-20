#include "access_control.h"

namespace solar_auth {

std::optional<std::string> resolve_folder_id(
    const std::string& folder_id,
    const std::string& user_id,
    solar_metadata::FolderDao& folder_dao,
    solar_http::HttpResponse& resp) {
    if (folder_id.empty()) {
        return std::string{};
    }

    auto folder = folder_dao.find_by_id(folder_id);
    if (!folder || folder->owner_id != user_id) {
        resp.set_error(404, "folder not found");
        return std::nullopt;
    }
    return folder_id;
}

std::optional<solar_metadata::FileRecord> require_owned_file(
    const std::string& user_id,
    const std::string& file_id,
    solar_metadata::FileDao& file_dao,
    solar_http::HttpResponse& resp) {
    if (user_id.empty()) {
        resp.set_error(401, "Unauthorized");
        return std::nullopt;
    }

    auto file = file_dao.find_owned_by_id(user_id, file_id);
    if (!file) {
        resp.set_error(404, "file not found");
        return std::nullopt;
    }
    return file;
}

} // namespace solar_auth
