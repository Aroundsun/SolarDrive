#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

#include "db_pool.h"

namespace solar_metadata {

// 文件元数据记录
struct FileRecord {
    std::string id;
    std::string name;
    int64_t     size;
    std::string hash;           // SHA-256
    std::string chunk_hashes;   // JSON array string
    std::string mime_type;
    std::string created_at;
};

// 文件元数据 DAO
// 负责 files 表的 CRUD 操作
class FileDao {
public:
    explicit FileDao(DbPool& pool);

    // 建表，包含 files 表和必要的索引
    void create_table();

    // 插入文件记录，返回生成的 UUID string
    std::string insert(const FileRecord& f);

    // 通过 id 查询
    std::optional<FileRecord> find_by_id(const std::string& id);

    // 通过 hash 查询（秒传用）
    std::optional<FileRecord> find_by_hash(const std::string& hash);

    // 软删除
    void soft_delete(const std::string& id);

private:
    DbPool& pool_;
};

} // namespace solar_metadata
