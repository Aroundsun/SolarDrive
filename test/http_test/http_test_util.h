#pragma once

#include <string>

#include "http_parser.h"

namespace solar_http::test {

inline HttpRequest parse_request(const std::string& raw) {
    HttpRequest parsed;
    HttpParser parser([&parsed](HttpRequest& req) {
        parsed = req;
    });
    parser.feed(raw.data(), raw.size());
    return parsed;
}

} // namespace solar_http::test
