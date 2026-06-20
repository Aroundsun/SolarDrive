#pragma once

#include "db_pool.h"

#include <pqxx/pqxx>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace solar_metadata {

struct ContentRecord {
    std::string id;
    std::string hash;
    int64_t     size = 0;
    std::string chunk_hashes;
    std::string mime_type;
};

/// content_objects 表访问：去重写入、引用计数、孤儿扫描
class ContentDao {
public:
    explicit ContentDao(DbPool& pool);

    /// 事务内按 hash 去重插入，返回 content_id
    std::string ensure_in_txn(pqxx::work& txn, const ContentRecord& content);

    std::optional<ContentRecord> find_by_hash(const std::string& hash);
    std::optional<ContentRecord> find_by_id(const std::string& id);

    /// 未被任何未删除 files 引用的 content 行数
    int active_refcount(const std::string& content_id);

    /// 无活跃引用的 content_objects（批处理 GC 用）
    std::vector<ContentRecord> list_orphans(int limit = 100);

    /// 事务内删除 content 行（再次校验无活跃引用）
    bool delete_by_id_in_txn(pqxx::work& txn, const std::string& content_id);

    /// chunk hash 是否仍被任意 content_objects 引用
    bool chunk_hash_in_use(pqxx::work& txn, const std::string& chunk_hash);

private:
    DbPool& pool_;
};

} // namespace solar_metadata
