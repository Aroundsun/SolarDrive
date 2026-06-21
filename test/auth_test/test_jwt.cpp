#include <gtest/gtest.h>

#include <string>

#include "jwt.h"

using solar_auth::JwtClaims;
using solar_auth::JwtUtil;

class JwtTest : public ::testing::Test {
protected:
    void SetUp() override {
        JwtUtil::set_secret("unit-test-secret");
    }
};

TEST_F(JwtTest, GenerateAndVerifyRoundTrip) {
    JwtClaims claims;
    claims.user_id  = "user-1";
    claims.username = "alice";
    claims.iat      = 1'700'000'000;
    claims.exp      = 1'800'000'000;

    const std::string token = JwtUtil::generate(claims, 168);
    const auto verified = JwtUtil::verify(token);

    ASSERT_TRUE(verified.has_value());
    EXPECT_EQ(verified->user_id, "user-1");
    EXPECT_EQ(verified->username, "alice");
}

TEST_F(JwtTest, RejectsTamperedToken) {
    JwtClaims claims;
    claims.user_id  = "user-1";
    claims.username = "alice";
    claims.iat      = 1'700'000'000;
    claims.exp      = 1'800'000'000;

    std::string token = JwtUtil::generate(claims, 168);
    token.back() = (token.back() == 'a') ? 'b' : 'a';

    EXPECT_FALSE(JwtUtil::verify(token).has_value());
}

TEST_F(JwtTest, RejectsExpiredToken) {
    JwtClaims claims;
    claims.user_id  = "user-1";
    claims.username = "alice";
    claims.iat      = 1;
    claims.exp      = 2;

    const std::string token = JwtUtil::generate(claims, 168);
    EXPECT_FALSE(JwtUtil::verify(token).has_value());
}

TEST_F(JwtTest, RejectsWrongSecret) {
    JwtClaims claims;
    claims.user_id  = "user-1";
    claims.username = "alice";
    claims.iat      = 1'700'000'000;
    claims.exp      = 1'800'000'000;

    const std::string token = JwtUtil::generate(claims, 168);

    JwtUtil::set_secret("other-secret");
    EXPECT_FALSE(JwtUtil::verify(token).has_value());
}

TEST_F(JwtTest, ExtractTokenFromBearerHeader) {
    const auto token = JwtUtil::extract_token("Bearer abc.def.ghi");
    ASSERT_TRUE(token.has_value());
    EXPECT_EQ(*token, "abc.def.ghi");
}

TEST_F(JwtTest, ExtractTokenRejectsInvalidPrefix) {
    EXPECT_FALSE(JwtUtil::extract_token("Token abc").has_value());
    EXPECT_FALSE(JwtUtil::extract_token("Bear").has_value());
}
