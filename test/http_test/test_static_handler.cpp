#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "http_request.h"
#include "http_response.h"
#include "static_handler.h"

using solar_http::HttpMethod;
using solar_http::HttpRequest;
using solar_http::HttpResponse;
using solar_http::StaticHandler;

namespace fs = std::filesystem;

class StaticHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / "solardrive_static_test";
        fs::remove_all(root_);
        fs::create_directories(root_);

        std::ofstream(root_ / "index.html") << "<html>home</html>";
        std::ofstream(root_ / "app.js") << "console.log('ok');";
        fs::create_directories(root_ / "nested");
        std::ofstream(root_ / "nested" / "page.html") << "nested";

        handler_ = std::make_unique<StaticHandler>(root_.string());
    }

    void TearDown() override {
        handler_.reset();
        fs::remove_all(root_);
    }

    HttpRequest make_get(const std::string& path) {
        HttpRequest req;
        req.set_method(HttpMethod::GET);
        req.set_path(path);
        return req;
    }

    fs::path root_;
    std::unique_ptr<StaticHandler> handler_;
};

TEST_F(StaticHandlerTest, RootMapsToIndexHtml) {
    HttpResponse resp;
    EXPECT_TRUE(handler_->try_serve(make_get("/"), resp));
    EXPECT_EQ(resp.status_code_, 200);
    EXPECT_EQ(resp.body_, "<html>home</html>");
    EXPECT_NE(resp.headers_.find("Content-Type"), resp.headers_.end());
}

TEST_F(StaticHandlerTest, ServesJsWithMime) {
    HttpResponse resp;
    EXPECT_TRUE(handler_->try_serve(make_get("/app.js"), resp));
    EXPECT_EQ(resp.body_, "console.log('ok');");
    EXPECT_EQ(resp.headers_.at("Content-Type"), "application/javascript; charset=utf-8");
}

TEST_F(StaticHandlerTest, RejectsPathTraversal) {
    HttpResponse resp;
    EXPECT_FALSE(handler_->try_serve(make_get("/../secret"), resp));
}

TEST_F(StaticHandlerTest, RejectsApiPaths) {
    HttpResponse resp;
    EXPECT_FALSE(handler_->try_serve(make_get("/api/v1/health"), resp));
}

TEST_F(StaticHandlerTest, RejectsNonGet) {
    HttpRequest req;
    req.set_method(HttpMethod::POST);
    req.set_path("/index.html");

    HttpResponse resp;
    EXPECT_FALSE(handler_->try_serve(req, resp));
}

TEST_F(StaticHandlerTest, MissingFileReturnsFalse) {
    HttpResponse resp;
    EXPECT_FALSE(handler_->try_serve(make_get("/missing.html"), resp));
}
