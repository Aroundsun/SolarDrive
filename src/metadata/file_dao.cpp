#include "file_dao.h"

#include <pqxx/pqxx>
#include <stdexcept>
#include <ctime>

namespace solar_metadata {

namespace {

constexpr const char* kFileSelect =
    "SELECT f.id::text, f.name, c.size, c.hash, c.chunk_hashes::text, c.mime_type, "
    "       to_char(f.created_at, 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') AS created_at, "
    "       f.owner_id::text, f.folder_id::text, f.content_id::text "
    "FROM files f "
    "JOIN content_objects c ON c.id = f.content_id ";

FileRecord row_to_record(const pqxx::row& row) {
    FileRecord rec;
    rec.id           = row[0].as<std::string>();
    rec.name         = row[1].as<std::string>();
    rec.size         = row[2].as<int64_t>();
    rec.hash         = row[3].as<std::string>();
    rec.chunk_hashes = row[4].as<std::string>();
    rec.mime_type    = row[5].as<std::string>();
    rec.created_at   = row[6].as<std::string>();
    rec.owner_id     = row[7].as<std::string>();
    if (!row[8].is_null()) {
        rec.folder_id = row[8].as<std::string>();
    }
    if (!row[9].is_null()) {
        rec.content_id = row[9].as<std::string>();
    }
    return rec;
}

ContentRecord file_to_content(const FileRecord& file) {
    ContentRecord content;
    content.hash         = file.hash;
    content.size         = file.size;
    content.chunk_hashes = file.chunk_hashes;
    content.mime_type    = file.mime_type;
    return content;
}

} // namespace

FileDao::FileDao(DbPool& pool, ContentDao& content_dao)
    : pool_(pool)
    , content_dao_(content_dao) {}

void FileDao::require_owner_id(const FileRecord& file) {
    if (file.owner_id.empty()) {
        throw std::invalid_argument("owner_id is required");
    }
}

void FileDao::create_table() {
    // Schema 由 bootstrap_schema() 统一管理
}

std::string FileDao::insert(const FileRecord& file) {
    require_owner_id(file);

    auto guard = pool_.acquire();
    pqxx::work txn(*guard);

    const std::string content_id = content_dao_.ensure_in_txn(txn, file_to_content(file));

    pqxx::row row = file.folder_id.empty()
        ? txn.exec_params1(
            "INSERT INTO files (name, owner_id, content_id) "
            "VALUES ($1, $2, $3) RETURNING id::text",
            file.name,
            file.owner_id,
            content_id)
        : txn.exec_params1(
            "INSERT INTO files (name, owner_id, folder_id, content_id) "
            "VALUES ($1, $2, $3, $4) RETURNING id::text",
            file.name,
            file.owner_id,
            file.folder_id,
            content_id);

    const std::string id = row[0].as<std::string>();
    txn.commit();
    return id;
}

std::string FileDao::insert_link(const FileRecord& file, const std::string& content_id) {
    require_owner_id(file);
    if (content_id.empty()) {
        throw std::invalid_argument("content_id is required");
    }

    auto guard = pool_.acquire();
    pqxx::work txn(*guard);

    pqxx::row row = file.folder_id.empty()
        ? txn.exec_params1(
            "INSERT INTO files (name, owner_id, content_id) "
            "VALUES ($1, $2, $3) RETURNING id::text",
            file.name,
            file.owner_id,
            content_id)
        : txn.exec_params1(
            "INSERT INTO files (name, owner_id, folder_id, content_id) "
            "VALUES ($1, $2, $3, $4) RETURNING id::text",
            file.name,
            file.owner_id,
            file.folder_id,
            content_id);

    const std::string id = row[0].as<std::string>();
    txn.commit();
    return id;
}

std::optional<FileRecord> FileDao::find_by_id(const std::string& id) {
    auto guard = pool_.acquire();
    pqxx::work txn(*guard);

    auto result = txn.exec_params(
        std::string(kFileSelect) + "WHERE f.id = $1 AND f.deleted_at IS NULL",
        id
    );
    if (result.empty()) {
        return std::nullopt;
    }
    return row_to_record(result[0]);
}

std::optional<FileRecord> FileDao::find_owned_by_id(const std::string& owner_id,
                                                    const std::string& id) {
    auto guard = pool_.acquire();
    pqxx::work txn(*guard);

    auto result = txn.exec_params(
        std::string(kFileSelect) +
        "WHERE f.id = $1 AND f.owner_id = $2 AND f.deleted_at IS NULL",
        id,
        owner_id
    );
    if (result.empty()) {
        return std::nullopt;
    }
    return row_to_record(result[0]);
}

std::optional<FileRecord> FileDao::find_content_by_hash(const std::string& hash) {
    auto content = content_dao_.find_by_hash(hash);
    if (!content) {
        return std::nullopt;
    }
    FileRecord rec;
    rec.content_id   = content->id;
    rec.id           = content->id;
    rec.size         = content->size;
    rec.hash         = content->hash;
    rec.chunk_hashes = content->chunk_hashes;
    rec.mime_type    = content->mime_type;
    return rec;
}

void FileDao::soft_delete(const std::string& id) {
    auto guard = pool_.acquire();
    pqxx::work txn(*guard);
    txn.exec_params("UPDATE files SET deleted_at = NOW() WHERE id = $1", id);
    txn.commit();
}

std::vector<FileRecord> FileDao::list_by_folder(const std::string& owner_id,
                                                const std::string& folder_id,
                                                int limit) {
    auto guard = pool_.acquire();
    pqxx::work txn(*guard);

    pqxx::result result = folder_id.empty()
        ? txn.exec_params(
            std::string(kFileSelect) +
            "WHERE f.deleted_at IS NULL AND f.owner_id = $1 AND f.folder_id IS NULL "
            "ORDER BY f.created_at DESC LIMIT $2",
            owner_id,
            limit)
        : txn.exec_params(
            std::string(kFileSelect) +
            "WHERE f.deleted_at IS NULL AND f.owner_id = $1 AND f.folder_id = $2 "
            "ORDER BY f.created_at DESC LIMIT $3",
            owner_id,
            folder_id,
            limit);

    std::vector<FileRecord> records;
    records.reserve(result.size());
    for (const auto& row : result) {
        records.push_back(row_to_record(row));
    }
    return records;
}

int FileDao::count_in_folder(const std::string& folder_id) {
    auto guard = pool_.acquire();
    pqxx::work txn(*guard);

    auto result = txn.exec_params(
        "SELECT COUNT(*)::int FROM files WHERE folder_id = $1 AND deleted_at IS NULL",
        folder_id
    );
    return result[0][0].as<int>();
}

bool FileDao::name_exists_in_folder(const std::string& owner_id,
                                    const std::string& folder_id,
                                    const std::string& name) {
    auto guard = pool_.acquire();
    pqxx::work txn(*guard);

    pqxx::result result = folder_id.empty()
        ? txn.exec_params(
            "SELECT 1 FROM files WHERE owner_id = $1 AND folder_id IS NULL "
            "AND name = $2 AND deleted_at IS NULL LIMIT 1",
            owner_id,
            name)
        : txn.exec_params(
            "SELECT 1 FROM files WHERE owner_id = $1 AND folder_id = $2 "
            "AND name = $3 AND deleted_at IS NULL LIMIT 1",
            owner_id,
            folder_id,
            name);

    return !result.empty();
}

std::string FileDao::make_unique_name(const std::string& name,
                                      const std::string& owner_id,
                                      const std::string& folder_id) {
    if (!name_exists_in_folder(owner_id, folder_id, name)) {
        return name;
    }

    std::string stem;
    std::string ext;
    const auto dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0) {
        stem = name.substr(0, dot);
        ext  = name.substr(dot);
    } else {
        stem = name;
    }

    for (int n = 1; n < 10000; ++n) {
        const std::string candidate = stem + "(" + std::to_string(n) + ")" + ext;
        if (!name_exists_in_folder(owner_id, folder_id, candidate)) {
            return candidate;
        }
    }

    return stem + "(" + std::to_string(static_cast<long long>(std::time(nullptr))) + ")" + ext;
}

} // namespace solar_metadata
