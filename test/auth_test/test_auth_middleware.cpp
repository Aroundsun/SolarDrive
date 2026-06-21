#include <gtest/gtest.h>

#include <string>

#include "auth_middleware.h"
#include "http_request.h"
#include "jwt.h"

using solar_auth::AuthMiddleware;
using solar_auth::JwtClaims;
using solar_auth::JwtUtil;
using solar_http::HttpMethod;
using solar_http::HttpRequest;

class AuthMiddlewareTest : public ::testing::Test {
protected:
    void SetUp() override {
        JwtUtil::set_secret("middleware-test-secret");
    }

    HttpRequest make_request(const std::string& path,
                             const std::string& authorization = "") {
        HttpRequest req;
        req.set_method(HttpMethod::GET);
        req.set_path(path);
        if (!authorization.empty()) {
            req.add_header("Authorization", authorization);
        }
        return req;
    }

    std::string make_token(const std::string& user_id, const std::string& username) {
        JwtClaims claims;
        claims.user_id  = user_id;
        claims.username = username;
        claims.iat      = 1'700'000'000;
        claims.exp      = 1'800'000'000;
        return JwtUtil::generate(claims, 168);
    }
};

TEST_F(AuthMiddlewareTest, WhitelistsHealthEndpoint) {
    EXPECT_TRUE(AuthMiddleware::is_whitelisted("/api/v1/health"));
}

TEST_F(AuthMiddlewareTest, WhitelistsStaticAssets) {
    EXPECT_TRUE(AuthMiddleware::is_whitelisted("/"));
    EXPECT_TRUE(AuthMiddleware::is_whitelisted("/login.html"));
    EXPECT_TRUE(AuthMiddleware::is_whitelisted("/app.js"));
}

TEST_F(AuthMiddlewareTest, WhitelistsPublicSharePath) {
    EXPECT_TRUE(AuthMiddleware::is_whitelisted("/s/abc123token"));
}

TEST_F(AuthMiddlewareTest, ProtectedPathRequiresAuthorization) {
    const auto req = make_request("/api/v1/files");
    const auto result = AuthMiddleware::authenticate(req);

    EXPECT_FALSE(result.authenticated);
    EXPECT_EQ(result.error_msg, "Missing Authorization header");
}

TEST_F(AuthMiddlewareTest, ValidTokenAuthenticatesUser) {
    const std::string token = make_token("uid-42", "bob");
    const auto req = make_request("/api/v1/files", "Bearer " + token);
    const auto result = AuthMiddleware::authenticate(req);

    EXPECT_TRUE(result.authenticated);
    EXPECT_EQ(result.user_id, "uid-42");
    EXPECT_EQ(result.username, "bob");
}

TEST_F(AuthMiddlewareTest, WhitelistedPathBypassesMissingAuth) {
    const auto req = make_request("/api/v1/health");
    const auto result = AuthMiddleware::authenticate(req);

    EXPECT_TRUE(result.authenticated);
    EXPECT_TRUE(result.user_id.empty());
}
