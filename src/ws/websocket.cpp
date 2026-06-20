#include "websocket.h"

#include <cstring>
#include <stdexcept>

#include <openssl/evp.h>

namespace solar_ws {

// ---------------------------------------------------------------------------
// WsCodec::decode
// ---------------------------------------------------------------------------
size_t WsCodec::decode(const char* data, size_t len, WsFrame& out_frame) {
    if (len < 2) {
        return 0;
    }

    // 第一字节：FIN + RSV + opcode
    out_frame.fin     = (data[0] >> 7) & 1;
    out_frame.opcode  = static_cast<OpCode>(data[0] & 0x0F);

    // 第二字节：MASK + payload length
    bool masked          = (data[1] >> 7) & 1;
    out_frame.masked     = masked;
    uint64_t payload_len = data[1] & 0x7F;

    size_t header_len = 2;
    if (payload_len == 126) {
        if (len < 4) return 0;
        payload_len = (static_cast<uint64_t>(static_cast<uint8_t>(data[2])) << 8) |
                       static_cast<uint64_t>(static_cast<uint8_t>(data[3]));
        header_len = 4;
    } else if (payload_len == 127) {
        if (len < 10) return 0;
        payload_len = 0;
        for (int i = 0; i < 8; ++i) {
            payload_len = (payload_len << 8) | static_cast<uint64_t>(static_cast<uint8_t>(data[2 + i]));
        }
        header_len = 10;
    }

    if (masked) {
        header_len += 4;
    }

    if (len < header_len + payload_len) {
        return 0;
    }

    // 拷贝 payload
    out_frame.payload.assign(data + header_len, data + header_len + payload_len);

    // 如果 masked，异或解码
    if (masked) {
        const uint8_t* mask_key = reinterpret_cast<const uint8_t*>(data + header_len - 4);
        for (uint64_t i = 0; i < payload_len; ++i) {
            out_frame.payload[i] ^= mask_key[i % 4];
        }
    }

    return header_len + static_cast<size_t>(payload_len);
}

// ---------------------------------------------------------------------------
// WsCodec::encode
// ---------------------------------------------------------------------------
std::string WsCodec::encode(OpCode opcode, const std::string& payload, bool fin) {
    std::string frame;

    // 第一字节
    uint8_t first_byte = 0x00;
    if (fin) {
        first_byte |= 0x80;
    }
    first_byte |= static_cast<uint8_t>(opcode);
    frame.push_back(static_cast<char>(first_byte));

    // 第二字节 + 扩展长度
    size_t payload_len = payload.size();
    if (payload_len <= 125) {
        frame.push_back(static_cast<char>(payload_len & 0x7F));
    } else if (payload_len <= 65535) {
        frame.push_back(static_cast<char>(126));
        frame.push_back(static_cast<char>((payload_len >> 8) & 0xFF));
        frame.push_back(static_cast<char>(payload_len & 0xFF));
    } else {
        frame.push_back(static_cast<char>(127));
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<char>((payload_len >> (i * 8)) & 0xFF));
        }
    }

    // payload
    frame.append(payload);

    return frame;
}

// ---------------------------------------------------------------------------
// WsHandshake::is_ws_upgrade
// ---------------------------------------------------------------------------
bool WsHandshake::is_ws_upgrade(const std::string& sec_websocket_key) {
    (void)sec_websocket_key;
    return true;
}

// ---------------------------------------------------------------------------
// WsHandshake::build_upgrade_response
// ---------------------------------------------------------------------------
std::string WsHandshake::build_upgrade_response(const std::string& key) {
    // 1. 拼接 GUID
    std::string concat = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    // 2. SHA-1 hash
    unsigned char hash[20] = {0};
    unsigned int hash_len = 0;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (ctx) {
        if (EVP_DigestInit_ex(ctx, EVP_sha1(), nullptr) &&
            EVP_DigestUpdate(ctx, concat.data(), concat.size()) &&
            EVP_DigestFinal_ex(ctx, hash, &hash_len)) {
            // success
        }
        EVP_MD_CTX_free(ctx);
    }

    // 3. Base64 编码（标准 Base64，带 = padding）
    int base64_len = 0;
    base64_len = 4 * ((hash_len + 2) / 3);
    std::string base64_str(static_cast<size_t>(base64_len) + 1, '\0');

    int encoded_len = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(&base64_str[0]),
                                      hash, static_cast<int>(hash_len));
    if (encoded_len >= 0) {
        base64_str.resize(static_cast<size_t>(encoded_len));
    } else {
        base64_str.clear();
    }

    // 4. 构建 HTTP 101 响应
    std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + base64_str + "\r\n"
        "\r\n";

    return response;
}

} // namespace solar_ws
