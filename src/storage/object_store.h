#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace solar_storage {

// 对象存储引擎
// 基于内容寻址（content-addressable），使用 SHA-256 作为对象 ID
// 支持去重（相同内容只存储一份）和分块存储
class ObjectStore {
public:
    static constexpr size_t CHUNK_SIZE = 4 * 1024 * 1024; // 4MB

    explicit ObjectStore(const std::string& base_path);

    // 写入数据，返回 SHA-256 hex string
    std::string put(const std::string& data);

    // 读取数据
    std::string get(const std::string& hash) const;

    // 检查是否存在
    bool exists(const std::string& hash) const;

    // 分块写入，返回每个 chunk 的 hash 列表
    std::vector<std::string> put_chunked(const std::string& data);

    // 从 chunk hash 列表还原完整数据
    std::string get_chunked(const std::vector<std::string>& hashes) const;

    // 计算 SHA-256
    static std::string sha256(const std::string& data);

private:
    // 将 hash 转换为存储路径: /base/ab/cd/<fullhash>
    std::string hash_to_path(const std::string& hash) const;

    std::string base_path_;
};

} // namespace solar_storage
