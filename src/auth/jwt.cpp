// ---------------------------------------------------------------------------
// jwt.cpp
//
// JWT 实现：Base64URL 编解码、HMAC-SHA256 签名、Token 生成与验证。
// 算法：HS256（Header.Payload.Signature 三段式）。
// ---------------------------------------------------------------------------

#include "jwt.h"
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <cstring>
#include <ctime>

using json = nlohmann::json;

namespace solar_auth {

// 默认密钥，生产环境必须通过 set_secret() 覆盖
std::string JwtUtil::secret_ = "solardrive-default-secret-change-me";

void JwtUtil::set_secret(const std::string& secret) {
    secret_ = secret;
}

// ---------- Base64URL 编解码 ----------

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string JwtUtil::base64url_encode(const unsigned char* data, size_t len) {
    // 先按标准 Base64 编码，再替换为 URL 安全字符并去除 padding
    static const char std_table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string out;
    out.reserve((len + 2) / 3 * 4);

    for (size_t i = 0; i < len; i += 3) {
        unsigned char b0 = data[i];
        unsigned char b1 = (i + 1 < len) ? data[i + 1] : 0;
        unsigned char b2 = (i + 2 < len) ? data[i + 2] : 0;

        out += std_table[b0 >> 2];
        out += std_table[((b0 & 0x03) << 4) | (b1 >> 4)];
        if (i + 1 < len)
            out += std_table[((b1 & 0x0F) << 2) | (b2 >> 6)];
        if (i + 2 < len)
            out += std_table[b2 & 0x3F];
    }
    // 去掉末尾 padding '='
    while (!out.empty() && out.back() == '=')
        out.pop_back();
    // '+' -> '-', '/' -> '_' 转为 URL 安全字符
    for (auto& c : out) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    return out;
}

std::optional<std::string> JwtUtil::base64url_decode(const std::string& str) {
    std::string input = str;
    // 还原为标准 Base64 字符
    for (auto& c : input) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }
    // 补齐 4 字节对齐所需的 padding
    while (input.size() % 4 != 0)
        input += '=';

    // 构建反向查表并逐块解码
    unsigned char dec[256];
    std::memset(dec, 0xFF, sizeof(dec));
    for (int i = 0; i < 64; i++)
        dec[(unsigned char)b64_table[i]] = i;

    std::string out;
    out.reserve(input.size() / 4 * 3);
    for (size_t i = 0; i < input.size(); i += 4) {
        unsigned char c0 = dec[(unsigned char)input[i]];
        unsigned char c1 = dec[(unsigned char)input[i + 1]];
        unsigned char c2 = dec[(unsigned char)input[i + 2]];
        unsigned char c3 = dec[(unsigned char)input[i + 3]];
        if (c0 == 0xFF || c1 == 0xFF || c2 == 0xFF || c3 == 0xFF)
            return std::nullopt;
        out += (c0 << 2) | (c1 >> 4);
        if (input[i + 2] != '=')
            out += ((c1 & 0x0F) << 4) | (c2 >> 2);
        if (input[i + 3] != '=')
            out += ((c2 & 0x03) << 6) | c3;
    }
    return out;
}

// ---------- HMAC-SHA256 签名 ----------

std::string JwtUtil::hmac_sha256(const std::string& key, const std::string& msg) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int  digest_len = 0;
    HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(msg.data()), msg.size(),
         digest, &digest_len);
    return std::string(reinterpret_cast<char*>(digest), digest_len);
}

// ---------- JWT 签发与验证 ----------

std::string JwtUtil::generate(const JwtClaims& claims, int ttl_hours) {
    // Header: 固定 HS256 算法
    json header;
    header["alg"] = "HS256";
    header["typ"] = "JWT";

    // Payload: 用户信息 + 签发/过期时间
    json payload;
    payload["user_id"]  = claims.user_id;
    payload["username"] = claims.username;
    payload["iat"]      = claims.iat ? claims.iat : std::time(nullptr);
    payload["exp"]      = claims.exp ? claims.exp : (payload["iat"].get<int64_t>() + ttl_hours * 3600);

    std::string header_b64  = base64url_encode(
        reinterpret_cast<const unsigned char*>(header.dump().data()),
        header.dump().size());
    std::string payload_b64 = base64url_encode(
        reinterpret_cast<const unsigned char*>(payload.dump().data()),
        payload.dump().size());

    // 签名输入 = base64url(header) + "." + base64url(payload)
    std::string signing_input = header_b64 + "." + payload_b64;
    std::string sig = hmac_sha256(secret_, signing_input);
    std::string sig_b64 = base64url_encode(
        reinterpret_cast<const unsigned char*>(sig.data()), sig.size());

    return signing_input + "." + sig_b64;
}

std::optional<JwtClaims> JwtUtil::verify(const std::string& token) {
    // JWT 格式：header.payload.signature，按 '.' 分割为三段
    auto dot1 = token.find('.');
    if (dot1 == std::string::npos) return std::nullopt;
    auto dot2 = token.find('.', dot1 + 1);
    if (dot2 == std::string::npos) return std::nullopt;

    std::string header_b64    = token.substr(0, dot1);
    std::string payload_b64   = token.substr(dot1 + 1, dot2 - dot1 - 1);
    std::string sig_b64       = token.substr(dot2 + 1);

    // 重新计算签名并与 token 中的签名比对
    std::string signing_input = header_b64 + "." + payload_b64;
    std::string expected_sig = hmac_sha256(secret_, signing_input);
    std::string expected_b64 = base64url_encode(
        reinterpret_cast<const unsigned char*>(expected_sig.data()),
        expected_sig.size());

    if (sig_b64 != expected_b64)
        return std::nullopt;

    // 解码 payload JSON 并提取用户声明
    auto payload_json_raw = base64url_decode(payload_b64);
    if (!payload_json_raw) return std::nullopt;

    try {
        json payload = json::parse(*payload_json_raw);

        JwtClaims claims;
        claims.user_id  = payload["user_id"].get<std::string>();
        claims.username = payload["username"].get<std::string>();
        claims.iat      = payload["iat"].get<int64_t>();
        claims.exp      = payload["exp"].get<int64_t>();

        // 拒绝已过期的 token
        int64_t now = static_cast<int64_t>(std::time(nullptr));
        if (claims.exp < now)
            return std::nullopt;

        return claims;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::string> JwtUtil::extract_token(const std::string& auth_header) {
    // 期望格式: "Bearer <token>" 或 "bearer <token>"
    if (auth_header.size() <= 7) return std::nullopt;
    std::string prefix = auth_header.substr(0, 7);
    // 前缀不区分大小写
    if (prefix != "Bearer " && prefix != "bearer ")
        return std::nullopt;
    return auth_header.substr(7);
}

} // namespace solar_auth
