#include "content_gc.h"

#include "../network/log.h"

#include <nlohmann/json.hpp>
#include <pqxx/pqxx>

namespace solar_metadata {

namespace {

std::vector<std::string> parse_chunk_hashes(const std::string& json_text) {
    const auto parsed = nlohmann::json::parse(json_text);
    std::vector<std::string> hashes;
    if (!parsed.is_array()) {
        return hashes;
    }
    for (const auto& item : parsed) {
        if (item.is_string()) {
            hashes.push_back(item.get<std::string>());
        }
    }
    return hashes;
}

} // namespace

ContentGc::ContentGc(ContentDao& content_dao,
                     DbPool& pool,
                     solar_storage::ObjectStore& store)
    : content_dao_(content_dao)
    , pool_(pool)
    , store_(store) {}

GcStats ContentGc::purge_content(const ContentRecord& content) {
    GcStats stats;
    if (content_dao_.active_refcount(content.id) > 0) {
        return stats;
    }

    std::vector<std::string> chunk_hashes;
    try {
        chunk_hashes = parse_chunk_hashes(content.chunk_hashes);
    } catch (const std::exception& e) {
        SNLOG_WARN("content GC: invalid chunk_hashes for {}: {}", content.id, e.what());
        return stats;
    }

    auto guard = pool_.acquire();
    pqxx::work txn(*guard);
    // 软删除的 files 行仍持有 content_id 外键，需先清理 tombstone 及关联分享
    txn.exec_params(
        "DELETE FROM file_shares "
        "WHERE file_id IN ("
        "  SELECT id FROM files WHERE content_id = $1 AND deleted_at IS NOT NULL"
        ")",
        content.id
    );
    txn.exec_params(
        "DELETE FROM files WHERE content_id = $1 AND deleted_at IS NOT NULL",
        content.id
    );
    if (!content_dao_.delete_by_id_in_txn(txn, content.id)) {
        return stats;
    }
    stats.content_rows_deleted = 1;

    for (const auto& hash : chunk_hashes) {
        if (!content_dao_.chunk_hash_in_use(txn, hash) && store_.remove(hash)) {
            stats.chunk_files_removed++;
        }
    }
    txn.commit();
    return stats;
}

GcStats ContentGc::collect_orphans(int batch_limit) {
    GcStats total;
    for (const auto& orphan : content_dao_.list_orphans(batch_limit)) {
        const GcStats one = purge_content(orphan);
        total.content_rows_deleted += one.content_rows_deleted;
        total.chunk_files_removed  += one.chunk_files_removed;
    }
    if (total.content_rows_deleted > 0) {
        SNLOG_INFO("content GC: removed {} content row(s), {} chunk file(s)",
                   total.content_rows_deleted, total.chunk_files_removed);
    }
    return total;
}

GcStats ContentGc::try_collect_content(const std::string& content_id) {
    if (content_id.empty()) {
        return {};
    }
    auto content = content_dao_.find_by_id(content_id);
    if (!content) {
        return {};
    }
    return purge_content(*content);
}

} // namespace solar_metadata
