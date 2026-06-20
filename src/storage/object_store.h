// =============================================================================
// object_store.h — 内容寻址对象存储
// SolarDrive 存储层：SHA-256 去重、分块读写、本地文件系统布局
// =============================================================================
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
    static constexpr size_t DEFAULT_CHUNK_SIZE = 4 * 1024 * 1024;

    explicit ObjectStore(const std::string& base_path,
                         size_t chunk_size = DEFAULT_CHUNK_SIZE);

    size_t chunk_size() const { return chunk_size_; }

    /// 写入整段数据，返回 SHA-256 hex；已存在则跳过写入（去重）
    std::string put(const std::string& data);

    /// 按 hash 读取完整对象
    std::string get(const std::string& hash) const;

    /// 检查对象是否已存在于存储目录
    bool exists(const std::string& hash) const;

    /// 分块写入，返回各 chunk 的 hash 列表（供元数据 chunk_hashes 字段）
    std::vector<std::string> put_chunked(const std::string& data);

    /// 按 chunk hash 顺序拼接还原完整文件内容
    std::string get_chunked(const std::vector<std::string>& hashes) const;

    /// 删除对象文件（不存在视为成功）
    bool remove(const std::string& hash);

    // 计算 SHA-256
    static std::string sha256(const std::string& data);

private:
    // 将 hash 转换为存储路径: /base/ab/cd/<fullhash>
    std::string hash_to_path(const std::string& hash) const;

    std::string base_path_;
    size_t      chunk_size_;
};

} // namespace solar_storage
