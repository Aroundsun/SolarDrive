#pragma once

#include "../http/http_response.h"
#include <cstdint>

namespace solar_api {

// 超过 max_bytes 时写 413 并返回 true
inline bool reject_oversized(int64_t size, int64_t max_bytes, solar_http::HttpResponse& resp) {
    if (max_bytes > 0 && size > max_bytes) {
        resp.set_error(413, "file too large");
        return true;
    }
    return false;
}

} // namespace solar_api
