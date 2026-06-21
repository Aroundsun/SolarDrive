#include <gtest/gtest.h>

#include <string>

#include "http_request.h"
#include "http_response.h"
#include "http_router.h"

using solar_http::HttpMethod;
using solar_http::HttpRequest;
using solar_http::HttpResponse;
using solar_http::HttpRouter;

TEST(HttpRouterTest, DispatchesExactPath) {
    HttpRouter router;
    bool called = false;
    router.get("/api/v1/health", [&](const HttpRequest&, HttpResponse& resp) {
        called = true;
        resp.set_json(R"({"status":"ok"})");
    });

    HttpRequest req;
    req.set_method(HttpMethod::GET);
    req.set_path("/api/v1/health");

    HttpResponse resp;
    router.dispatch(req, resp);

    EXPECT_TRUE(called);
    EXPECT_EQ(resp.status_code_, 200);
}

TEST(HttpRouterTest, ExtractsPathParams) {
    HttpRouter router;
    std::string captured_id;
    router.get("/api/v1/files/{id}", [&](const HttpRequest& req, HttpResponse& resp) {
        captured_id = req.get_path_param("id");
        resp.set_status(200, "OK");
    });

    HttpRequest req;
    req.set_method(HttpMethod::GET);
    req.set_path("/api/v1/files/abc-123");

    HttpResponse resp;
    router.dispatch(req, resp);

    EXPECT_EQ(captured_id, "abc-123");
    EXPECT_EQ(resp.status_code_, 200);
}

TEST(HttpRouterTest, MethodMismatchReturns404) {
    HttpRouter router;
    router.post("/api/v1/upload", [&](const HttpRequest&, HttpResponse& resp) {
        resp.set_status(200, "OK");
    });

    HttpRequest req;
    req.set_method(HttpMethod::GET);
    req.set_path("/api/v1/upload");

    HttpResponse resp;
    router.dispatch(req, resp);

    EXPECT_EQ(resp.status_code_, 404);
}

TEST(HttpRouterTest, UnknownPathReturns404) {
    HttpRouter router;
    router.get("/known", [&](const HttpRequest&, HttpResponse&) {});

    HttpRequest req;
    req.set_method(HttpMethod::GET);
    req.set_path("/unknown");

    HttpResponse resp;
    router.dispatch(req, resp);

    EXPECT_EQ(resp.status_code_, 404);
    EXPECT_NE(resp.body_.find("Not Found"), std::string::npos);
}
