#include <gtest/gtest.h>

#include <string>

#include "http_response.h"

using solar_http::HttpResponse;

TEST(HttpResponseTest, SerializeCloseByDefault) {
    HttpResponse resp;
    resp.set_body("ok");

    const std::string raw = resp.serialize();

    EXPECT_NE(raw.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(raw.find("Connection: close\r\n"), std::string::npos);
    EXPECT_NE(raw.find("Content-Length: 2\r\n"), std::string::npos);
    EXPECT_NE(raw.find("\r\n\r\nok"), std::string::npos);
}

TEST(HttpResponseTest, SerializeKeepAliveAddsHeaders) {
    HttpResponse resp;
    resp.set_close_connection(false);
    resp.set_body("ok");

    const std::string raw = resp.serialize();

    EXPECT_NE(raw.find("Connection: keep-alive\r\n"), std::string::npos);
    EXPECT_NE(raw.find("Keep-Alive: timeout=60, max=1000\r\n"), std::string::npos);
}

TEST(HttpResponseTest, SetJsonSetsContentType) {
    HttpResponse resp;
    resp.set_json(R"({"status":"ok"})");

    EXPECT_NE(resp.headers_.find("Content-Type"), resp.headers_.end());
    EXPECT_EQ(resp.headers_.at("Content-Type"), "application/json; charset=utf-8");
    EXPECT_EQ(resp.body_, R"({"status":"ok"})");
}

TEST(HttpResponseTest, SetErrorProducesJsonBody) {
    HttpResponse resp;
    resp.set_error(401, "Unauthorized");

    EXPECT_EQ(resp.status_code_, 401);
    EXPECT_NE(resp.body_.find("\"error\":\"Unauthorized\""), std::string::npos);
    EXPECT_NE(resp.serialize().find("HTTP/1.1 401 Unauthorized"), std::string::npos);
}

TEST(HttpResponseTest, CustomConnectionHeaderNotOverwritten) {
    HttpResponse resp;
    resp.set_header("Connection", "upgrade");
    resp.set_body("");

    const std::string raw = resp.serialize();
    EXPECT_EQ(raw.find("Connection: close"), std::string::npos);
    EXPECT_NE(raw.find("Connection: upgrade"), std::string::npos);
}
