#include <gtest/gtest.h>

#include "http_request.h"
#include "query_utils.h"

using solar_http::HttpMethod;
using solar_http::HttpRequest;

namespace {

HttpRequest make_request(const std::string& version,
                         const std::string& connection_header) {
    HttpRequest req;
    req.set_method(HttpMethod::GET);
    req.set_path("/");
    req.set_version(version);
    if (!connection_header.empty()) {
        req.add_header("Connection", connection_header);
    }
    return req;
}

} // namespace

TEST(HttpRequestTest, GetHeaderIcIsCaseInsensitive) {
    HttpRequest req;
    req.add_header("Authorization", "Bearer token");

    EXPECT_EQ(req.get_header_ic("authorization"), "Bearer token");
    EXPECT_EQ(req.get_header_ic("AUTHORIZATION"), "Bearer token");
}

TEST(HttpRequestTest, WantsKeepAliveByDefaultForHttp11) {
    const auto req = make_request("HTTP/1.1", "");
    EXPECT_TRUE(req.wants_keep_alive());
}

TEST(HttpRequestTest, WantsKeepAliveExplicitHeader) {
    const auto req = make_request("HTTP/1.0", "keep-alive");
    EXPECT_TRUE(req.wants_keep_alive());
}

TEST(HttpRequestTest, RejectsConnectionClose) {
    const auto req = make_request("HTTP/1.1", "close");
    EXPECT_FALSE(req.wants_keep_alive());
}

TEST(HttpRequestTest, Http10DefaultsToClose) {
    const auto req = make_request("HTTP/1.0", "");
    EXPECT_FALSE(req.wants_keep_alive());
}

TEST(HttpRequestTest, ContentLengthFromHeader) {
    HttpRequest req;
    req.add_header("Content-Length", "42");
    EXPECT_EQ(req.content_length(), 42u);
}

TEST(HttpRequestTest, PathParamsRoundTrip) {
    HttpRequest req;
    req.set_path_param("id", "file-123");
    EXPECT_EQ(req.get_path_param("id"), "file-123");
    EXPECT_EQ(req.get_path_param("missing"), "");
}

TEST(HttpRequestTest, QueryParamExtraction) {
    HttpRequest req;
    req.set_query("token=abc123&mode=fast");

    EXPECT_EQ(solar_http::get_query_param(req, "token"), "abc123");
    EXPECT_EQ(solar_http::get_query_param(req, "mode"), "fast");
    EXPECT_EQ(solar_http::get_query_param(req, "missing"), "");
}
