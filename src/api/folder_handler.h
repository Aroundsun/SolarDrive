#pragma once

#include "../http/http_request.h"
#include "../http/http_response.h"
#include "../metadata/folder_dao.h"
#include "../metadata/file_dao.h"

namespace solar_api {

class FolderHandler {
public:
    FolderHandler(solar_metadata::FolderDao& folder_dao,
                  solar_metadata::FileDao& file_dao);

    void handle_list(const solar_http::HttpRequest& req, solar_http::HttpResponse& resp);
    void handle_create(const solar_http::HttpRequest& req, solar_http::HttpResponse& resp);
    void handle_delete(const solar_http::HttpRequest& req, solar_http::HttpResponse& resp);
    void handle_rename(const solar_http::HttpRequest& req, solar_http::HttpResponse& resp);

private:
    solar_metadata::FolderDao& folder_dao_;
    solar_metadata::FileDao& file_dao_;
};

} // namespace solar_api
