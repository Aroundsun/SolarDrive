#pragma once

#include "../http/http_request.h"
#include "../http/http_response.h"
#include "../metadata/file_dao.h"
#include "../metadata/share_dao.h"
#include "../metadata/content_gc.h"

namespace solar_api {

class FileHandler {
public:
    FileHandler(solar_metadata::FileDao& file_dao,
                solar_metadata::ShareDao& share_dao,
                solar_metadata::ContentGc& content_gc);

    void handle_list(const solar_http::HttpRequest& req, solar_http::HttpResponse& resp);
    void handle_delete(const solar_http::HttpRequest& req, solar_http::HttpResponse& resp);

private:
    solar_metadata::FileDao&   file_dao_;
    solar_metadata::ShareDao&  share_dao_;
    solar_metadata::ContentGc& content_gc_;
};

} // namespace solar_api
