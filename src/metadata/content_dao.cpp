#include "content_dao.h"

#include <pqxx/pqxx>
#include <stdexcept>

namespace solar_metadata {

namespace {

ContentRecord row_to_content(const pqxx::row& row) {
    ContentRecord rec;
    rec.id           = row[0].as<std::string>();
    rec.hash         = row[1].as<std::string>();
    rec.size         = row[2].as<int64_t>();
    rec.chunk_hashes = row[3].as<std::string>();
    rec.mime_type    = row[4].as<std::string>();
    return rec;
}

} // namespace

ContentDao::ContentDao(DbPool& pool) : pool_(pool) {}

std::string ContentDao::ensure_in_txn(pqxx::work& txn, const ContentRecord& content) {
    auto existing = txn.exec_params(
        "SELECT id::text FROM content_objects WHERE hash = $1",
        content.hash
    );
    if (!existing.empty()) {
        return existing[0][0].as<std::string>();
    }

    auto inserted = txn.exec_params(
        "INSERT INTO content_objects (hash, size, chunk_hashes, mime_type) "
        "VALUES ($1, $2, $3, $4) "
        "ON CONFLICT (hash) DO NOTHING RETURNING id::text",
        content.hash,
        content.size,
        content.chunk_hashes,
        content.mime_type.empty() ? "application/octet-stream" : content.mime_type
    );
    if (!inserted.empty()) {
        return inserted[0][0].as<std::string>();
    }

    auto raced = txn.exec_params(
        "SELECT id::text FROM content_objects WHERE hash = $1",
        content.hash
    );
    if (raced.empty()) {
        throw std::runtime_error("failed to resolve content object for hash");
    }
    return raced[0][0].as<std::string>();
}

std::optional<ContentRecord> ContentDao::find_by_hash(const std::string& hash) {
    auto guard = pool_.acquire();
    pqxx::work txn(*guard);
    auto result = txn.exec_params(
        "SELECT id::text, hash, size, chunk_hashes::text, mime_type "
        "FROM content_objects WHERE hash = $1",
        hash
    );
    if (result.empty()) {
        return std::nullopt;
    }
    return row_to_content(result[0]);
}

std::optional<ContentRecord> ContentDao::find_by_id(const std::string& id) {
    auto guard = pool_.acquire();
    pqxx::work txn(*guard);
    auto result = txn.exec_params(
        "SELECT id::text, hash, size, chunk_hashes::text, mime_type "
        "FROM content_objects WHERE id = $1",
        id
    );
    if (result.empty()) {
        return std::nullopt;
    }
    return row_to_content(result[0]);
}

int ContentDao::active_refcount(const std::string& content_id) {
    auto guard = pool_.acquire();
    pqxx::work txn(*guard);
    auto result = txn.exec_params(
        "SELECT COUNT(*)::int FROM files "
        "WHERE content_id = $1 AND deleted_at IS NULL",
        content_id
    );
    return result[0][0].as<int>();
}

std::vector<ContentRecord> ContentDao::list_orphans(int limit) {
    auto guard = pool_.acquire();
    pqxx::work txn(*guard);
    auto result = txn.exec_params(
        "SELECT c.id::text, c.hash, c.size, c.chunk_hashes::text, c.mime_type "
        "FROM content_objects c "
        "WHERE NOT EXISTS ("
        "  SELECT 1 FROM files f "
        "  WHERE f.content_id = c.id AND f.deleted_at IS NULL"
        ") "
        "LIMIT $1",
        limit
    );

    std::vector<ContentRecord> records;
    records.reserve(result.size());
    for (const auto& row : result) {
        records.push_back(row_to_content(row));
    }
    return records;
}

bool ContentDao::delete_by_id_in_txn(pqxx::work& txn, const std::string& content_id) {
    auto result = txn.exec_params(
        "DELETE FROM content_objects c "
        "WHERE c.id = $1 AND NOT EXISTS ("
        "  SELECT 1 FROM files f "
        "  WHERE f.content_id = c.id AND f.deleted_at IS NULL"
        ") "
        "RETURNING c.id::text",
        content_id
    );
    return !result.empty();
}

bool ContentDao::chunk_hash_in_use(pqxx::work& txn, const std::string& chunk_hash) {
    auto result = txn.exec_params(
        "SELECT 1 FROM content_objects "
        "WHERE chunk_hashes @> jsonb_build_array($1::text) LIMIT 1",
        chunk_hash
    );
    return !result.empty();
}

} // namespace solar_metadata
