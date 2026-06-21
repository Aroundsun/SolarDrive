#include <gtest/gtest.h>

#include <string>

#include "http_parser.h"
#include "http_test_util.h"

using solar_http::HttpMethod;
using solar_http::HttpParser;
using solar_http::HttpRequest;
using solar_http::test::parse_request;

TEST(HttpParserTest, ParsesSimpleGet) {
    const auto req = parse_request("GET /api/v1/health HTTP/1.1\r\n\r\n");

    EXPECT_EQ(req.method(), HttpMethod::GET);
    EXPECT_EQ(req.path(), "/api/v1/health");
    EXPECT_EQ(req.query(), "");
    EXPECT_TRUE(req.body().empty());
}

TEST(HttpParserTest, ParsesPathAndQuery) {
    const auto req = parse_request(
        "GET /files?id=1&name=abc HTTP/1.1\r\n\r\n");

    EXPECT_EQ(req.path(), "/files");
    EXPECT_EQ(req.query(), "id=1&name=abc");
}

TEST(HttpParserTest, ParsesHeaders) {
    const auto req = parse_request(
        "GET / HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: keep-alive\r\n"
        "\r\n");

    EXPECT_EQ(req.get_header("Host"), "localhost");
    EXPECT_EQ(req.get_header("Connection"), "keep-alive");
}

TEST(HttpParserTest, ParsesPostWithBody) {
    const std::string body = R"({"username":"alice"})";
    const std::string raw =
        "POST /api/v1/auth/login HTTP/1.1\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "\r\n" + body;

    const auto req = parse_request(raw);

    EXPECT_EQ(req.method(), HttpMethod::POST);
    EXPECT_EQ(req.path(), "/api/v1/auth/login");
    EXPECT_EQ(req.get_header("Content-Type"), "application/json");
    EXPECT_EQ(req.body(), body);
}

TEST(HttpParserTest, ParsesDeleteMethod) {
    const auto req = parse_request("DELETE /api/v1/files/abc HTTP/1.1\r\n\r\n");

    EXPECT_EQ(req.method(), HttpMethod::DELETE);
    EXPECT_EQ(req.path(), "/api/v1/files/abc");
}

TEST(HttpParserTest, ResetAllowsSecondRequest) {
    HttpRequest first;
    HttpRequest second;
    HttpParser parser([&](HttpRequest& req) {
        if (first.path().empty()) {
            first = req;
        } else {
            second = req;
        }
    });

    parser.feed("GET /first HTTP/1.1\r\n\r\n", 24);
    parser.feed("GET /second HTTP/1.1\r\n\r\n", 25);

    EXPECT_EQ(first.path(), "/first");
    EXPECT_EQ(second.path(), "/second");
}
