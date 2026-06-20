// =============================================================================
// share_dao.cpp — 文件分享数据访问层实现
// =============================================================================
#include "share_dao.h"
#include <pqxx/pqxx>

namespace solar_api {

ShareDao::ShareDao(solar_metadata::DbPool& pool) : pool_(pool) {}

void ShareDao::create_table() {
    // Schema 由 bootstrap_schema() 统一管理
}

std::string ShareDao::insert(const ShareRecord& record) {
    auto g = pool_.acquire();
    pqxx::work tx(*g);

    const std::string expires = record.expires_at.empty()
        ? std::string{}
        : record.expires_at;
    const std::string password = record.password_hash.empty()
        ? std::string{}
        : record.password_hash;

    pqxx::row row = tx.exec_params1(
        "INSERT INTO file_shares "
        "(file_id, owner_id, share_token, password_hash, expires_at, max_downloads) "
        "VALUES ($1, $2, $3, NULLIF($4, ''), "
        "        CASE WHEN $5 = '' THEN NULL ELSE $5::timestamptz END, $6) "
        "RETURNING id::text",
        record.file_id,
        record.owner_id,
        record.share_token,
        password,
        expires,
        record.max_downloads
    );

    tx.commit();
    return row[0].as<std::string>();
}

std::optional<ShareRecord> ShareDao::find_by_token(const std::string& token) {
    auto g = pool_.acquire();
    pqxx::work tx(*g);
    auto r = tx.exec_params(
        "SELECT id, file_id, owner_id, share_token, "
        "COALESCE(password_hash, ''), "
        "COALESCE(expires_at::text, ''), "
        "max_downloads, download_count, created_at::text, is_revoked "
        "FROM file_shares WHERE share_token = $1",
        token
    );
    if (r.empty()) return std::nullopt;

    auto row = r[0];
    ShareRecord s;
    s.id             = row[0].as<std::string>();
    s.file_id        = row[1].as<std::string>();
    s.owner_id       = row[2].as<std::string>();
    s.share_token    = row[3].as<std::string>();
    s.password_hash  = row[4].as<std::string>();
    s.expires_at     = row[5].as<std::string>();
    s.max_downloads  = row[6].as<int>();
    s.download_count = row[7].as<int>();
    s.created_at     = row[8].as<std::string>();
    s.is_revoked     = row[9].as<bool>();
    return s;
}

std::vector<ShareRecord> ShareDao::list_by_owner(const std::string& owner_id, int limit, int offset) {
    auto g = pool_.acquire();
    pqxx::work tx(*g);
    auto r = tx.exec_params(
        "SELECT id, file_id, owner_id, share_token, "
        "COALESCE(password_hash, ''), "
        "COALESCE(expires_at::text, ''), "
        "max_downloads, download_count, created_at::text, is_revoked "
        "FROM file_shares WHERE owner_id = $1 AND is_revoked = FALSE "
        "ORDER BY created_at DESC LIMIT $2 OFFSET $3",
        owner_id, limit, offset
    );

    std::vector<ShareRecord> result;
    for (auto row : r) {
        ShareRecord s;
        s.id             = row[0].as<std::string>();
        s.file_id        = row[1].as<std::string>();
        s.owner_id       = row[2].as<std::string>();
        s.share_token    = row[3].as<std::string>();
        s.password_hash  = row[4].as<std::string>();
        s.expires_at     = row[5].as<std::string>();
        s.max_downloads  = row[6].as<int>();
        s.download_count = row[7].as<int>();
        s.created_at     = row[8].as<std::string>();
        s.is_revoked     = row[9].as<bool>();
        result.push_back(s);
    }
    return result;
}

void ShareDao::increment_download_count(const std::string& id) {
    auto g = pool_.acquire();
    pqxx::work tx(*g);
    tx.exec_params("UPDATE file_shares SET download_count = download_count + 1 WHERE id = $1", id);
    tx.commit();
}

void ShareDao::revoke(const std::string& id, const std::string& owner_id) {
    auto g = pool_.acquire();
    pqxx::work tx(*g);
    tx.exec_params(
        "UPDATE file_shares SET is_revoked = TRUE WHERE id = $1 AND owner_id = $2",
        id, owner_id
    );
    tx.commit();
}

bool ShareDao::token_exists(const std::string& token) {
    auto g = pool_.acquire();
    pqxx::work tx(*g);
    auto r = tx.exec_params("SELECT 1 FROM file_shares WHERE share_token = $1", token);
    return !r.empty();
}

} // namespace solar_api
