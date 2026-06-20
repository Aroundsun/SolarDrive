#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <memory>

namespace solar_ws {

// WebSocket 操作码
enum class OpCode : uint8_t {
    kContinuation = 0x0,
    kText         = 0x1,
    kBinary       = 0x2,
    kClose        = 0x8,
    kPing         = 0x9,
    kPong         = 0xA
};

// 解析后的帧
struct WsFrame {
    OpCode     opcode;
    bool       fin;
    bool       masked;
    std::string payload;
};

// WebSocket 帧编码器/解码器
class WsCodec {
public:
    // 解析一个帧（从缓冲区数据），返回消耗的字节数。帧不完整时返回 0
    static size_t decode(const char* data, size_t len, WsFrame& out_frame);

    // 编码一帧（服务端 → 客户端，不 mask），返回编码后的数据
    static std::string encode(OpCode opcode, const std::string& payload, bool fin = true);
};

// WebSocket 握手
class WsHandshake {
public:
    // 验证是否为 WebSocket 升级请求
    static bool is_ws_upgrade(const std::string& sec_websocket_key);

    // 生成 101 Switching Protocols 响应（完整 HTTP 响应字符串，可直接 send）
    static std::string build_upgrade_response(const std::string& key);
};

} // namespace solar_ws
