#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "websocket.h"

using solar_ws::OpCode;
using solar_ws::WsCodec;
using solar_ws::WsFrame;
using solar_ws::WsHandshake;

TEST(WsCodecTest, EncodeDecodeTextRoundTrip) {
    const std::string payload = "hello websocket";
    const std::string frame = WsCodec::encode(OpCode::kText, payload);

    WsFrame decoded;
    const size_t consumed = WsCodec::decode(frame.data(), frame.size(), decoded);

    ASSERT_EQ(consumed, frame.size());
    EXPECT_TRUE(decoded.fin);
    EXPECT_FALSE(decoded.masked);
    EXPECT_EQ(decoded.opcode, OpCode::kText);
    EXPECT_EQ(decoded.payload, payload);
}

TEST(WsCodecTest, DecodeMaskedClientFrame) {
    // FIN + text opcode, masked, payload len=3, mask key, payload XORed
    const unsigned char raw[] = {
        0x81, 0x83, 0x12, 0x34, 0x56, 0x78,
        'h' ^ 0x12, 'i' ^ 0x34, '!' ^ 0x56,
    };

    WsFrame decoded;
    const size_t consumed = WsCodec::decode(
        reinterpret_cast<const char*>(raw), sizeof(raw), decoded);

    ASSERT_EQ(consumed, sizeof(raw));
    EXPECT_TRUE(decoded.masked);
    EXPECT_EQ(decoded.payload, "hi!");
}

TEST(WsCodecTest, IncompleteFrameReturnsZero) {
    const std::string frame = WsCodec::encode(OpCode::kBinary, "abc");

    WsFrame decoded;
    EXPECT_EQ(WsCodec::decode(frame.data(), 1, decoded), 0u);
}

TEST(WsHandshakeTest, BuildUpgradeResponseUsesRfcExample) {
    const std::string key = "dGhlIHNhbXBsZSBub25jZQ==";
    const std::string response = WsHandshake::build_upgrade_response(key);

    EXPECT_NE(response.find("HTTP/1.1 101 Switching Protocols"), std::string::npos);
    EXPECT_NE(response.find("Upgrade: websocket"), std::string::npos);
    EXPECT_NE(response.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo="),
                std::string::npos);
}

TEST(WsHandshakeTest, IsWsUpgradeAlwaysTrueForNonEmptyKey) {
    EXPECT_TRUE(WsHandshake::is_ws_upgrade("dGhlIHNhbXBsZSBub25jZQ=="));
}
