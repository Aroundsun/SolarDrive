#pragma once

#include "../http/http_request.h"
#include "../http/http_response.h"
#include "../metadata/file_dao.h"
#include "../metadata/folder_dao.h"
#include <optional>
#include <string>

namespace solar_auth {

std::optional<std::string> resolve_folder_id(
    const std::string& folder_id,
    const std::string& user_id,
    solar_metadata::FolderDao& folder_dao,
    solar_http::HttpResponse& resp);

// 查找用户拥有的文件，失败时写 resp
std::optional<solar_metadata::FileRecord> require_owned_file(
    const std::string& user_id,
    const std::string& file_id,
    solar_metadata::FileDao& file_dao,
    solar_http::HttpResponse& resp);

} // namespace solar_auth
