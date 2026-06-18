#include "file_dao.h"

#include <pqxx/pqxx>
#include <pqxx/except>
#include <stdexcept>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace solar_metadata {

FileDao::FileDao(DbPool& pool)
    : pool_(pool)
{
}

void FileDao::create_table()
{
    auto guard = pool_.acquire();
    pqxx::work txn(*guard);

    txn.exec(
        "CREATE TABLE IF NOT EXISTS files ("
        "    id           UUID PRIMARY KEY DEFAULT gen_random_uuid(),"
        "    name         TEXT NOT NULL,"
        "    size         BIGINT NOT NULL,"
        "    hash         TEXT NOT NULL,"
        "    chunk_hashes JSONB NOT NULL,"
        "    mime_type    TEXT NOT NULL DEFAULT 'application/octet-stream',"
        "    created_at   TIMESTAMPTZ NOT NULL DEFAULT NOW(),"
        "    deleted_at   TIMESTAMPTZ"
        ")"
    );

    // 索引
    txn.exec("CREATE INDEX IF NOT EXISTS idx_files_hash ON files(hash) WHERE deleted_at IS NULL");
    txn.exec("CREATE INDEX IF NOT EXISTS idx_files_created_at ON files(created_at DESC)");

    txn.commit();
}

std::string FileDao::insert(const FileRecord& f)
{
    auto guard = pool_.acquire();
    pqxx::work txn(*guard);

    // 如果 id 为空，使用 DEFAULT 让数据库生成 UUID
    std::string sql;
    pqxx::params params;

    if (f.id.empty()) {
        sql =
            "INSERT INTO files (name, size, hash, chunk_hashes, mime_type) "
            "VALUES ($1, $2, $3, $4, $5) "
            "RETURNING id::text";
        params.append(f.name);
        params.append(f.size);
        params.append(f.hash);
        params.append(f.chunk_hashes);
        params.append(f.mime_type);
    } else {
        sql =
            "INSERT INTO files (id, name, size, hash, chunk_hashes, mime_type) "
            "VALUES ($1, $2, $3, $4, $5, $6) "
            "RETURNING id::text";
        params.append(f.id);
        params.append(f.name);
        params.append(f.size);
        params.append(f.hash);
        params.append(f.chunk_hashes);
        params.append(f.mime_type);
    }

    auto row = txn.exec_params1(sql, params);
    std::string generated_id = row[0].as<std::string>();

    txn.commit();
    return generated_id;
}

std::optional<FileRecord> FileDao::find_by_id(const std::string& id)
{
    auto guard = pool_.acquire();
    pqxx::work txn(*guard);

    auto result = txn.exec_params(
        "SELECT id::text, name, size, hash, chunk_hashes::text, mime_type, "
        "       to_char(created_at, 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') as created_at "
        "FROM files "
        "WHERE id = $1 AND deleted_at IS NULL",
        pqxx::params(id)
    );

    if (result.empty()) {
        return std::nullopt;
    }

    auto row = result[0];
    FileRecord rec;
    rec.id           = row[0].as<std::string>();
    rec.name         = row[1].as<std::string>();
    rec.size         = row[2].as<int64_t>();
    rec.hash         = row[3].as<std::string>();
    rec.chunk_hashes = row[4].as<std::string>();
    rec.mime_type    = row[5].as<std::string>();
    rec.created_at   = row[6].as<std::string>();

    return rec;
}

std::optional<FileRecord> FileDao::find_by_hash(const std::string& hash)
{
    auto guard = pool_.acquire();
    pqxx::work txn(*guard);

    auto result = txn.exec_params(
        "SELECT id::text, name, size, hash, chunk_hashes::text, mime_type, "
        "       to_char(created_at, 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') as created_at "
        "FROM files "
        "WHERE hash = $1 AND deleted_at IS NULL "
        "LIMIT 1",
        pqxx::params(hash)
    );

    if (result.empty()) {
        return std::nullopt;
    }

    auto row = result[0];
    FileRecord rec;
    rec.id           = row[0].as<std::string>();
    rec.name         = row[1].as<std::string>();
    rec.size         = row[2].as<int64_t>();
    rec.hash         = row[3].as<std::string>();
    rec.chunk_hashes = row[4].as<std::string>();
    rec.mime_type    = row[5].as<std::string>();
    rec.created_at   = row[6].as<std::string>();

    return rec;
}

void FileDao::soft_delete(const std::string& id)
{
    auto guard = pool_.acquire();
    pqxx::work txn(*guard);

    txn.exec_params(
        "UPDATE files SET deleted_at = NOW() WHERE id = $1",
        pqxx::params(id)
    );

    txn.commit();
}

} // namespace solar_metadata
