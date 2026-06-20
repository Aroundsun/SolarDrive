#include "folder_dao.h"

#include <pqxx/pqxx>
#include <stdexcept>

namespace solar_metadata {

FolderDao::FolderDao(DbPool& pool) : pool_(pool) {}

void FolderDao::create_table()
{
    // Schema 由 bootstrap_schema() 统一管理
}

std::string FolderDao::insert(const FolderRecord& folder)
{
    auto guard = pool_.acquire();
    pqxx::work txn(*guard);

    pqxx::row row = folder.parent_id.empty()
        ? txn.exec_params1(
            "INSERT INTO folders (name, owner_id) "
            "VALUES ($1, $2) RETURNING id::text",
            folder.name,
            folder.owner_id)
        : txn.exec_params1(
            "INSERT INTO folders (name, parent_id, owner_id) "
            "VALUES ($1, $2, $3) RETURNING id::text",
            folder.name,
            folder.parent_id,
            folder.owner_id);

    const std::string id = row[0].as<std::string>();
    txn.commit();
    return id;
}

std::optional<FolderRecord> FolderDao::find_by_id(const std::string& id)
{
    auto guard = pool_.acquire();
    pqxx::work txn(*guard);

    auto result = txn.exec_params(
        "SELECT id::text, name, parent_id::text, owner_id::text, "
        "       to_char(created_at, 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') "
        "FROM folders WHERE id = $1 AND deleted_at IS NULL",
        id
    );

    if (result.empty()) {
        return std::nullopt;
    }

    FolderRecord rec;
    auto row = result[0];
    rec.id = row[0].as<std::string>();
    rec.name = row[1].as<std::string>();
    if (!row[2].is_null()) {
        rec.parent_id = row[2].as<std::string>();
    }
    rec.owner_id = row[3].as<std::string>();
    rec.created_at = row[4].as<std::string>();
    return rec;
}

std::vector<FolderRecord> FolderDao::list_by_parent(const std::string& owner_id,
                                                    const std::string& parent_id,
                                                    int limit)
{
    auto guard = pool_.acquire();
    pqxx::work txn(*guard);

    pqxx::result result = parent_id.empty()
        ? txn.exec_params(
            "SELECT id::text, name, parent_id::text, owner_id::text, "
            "       to_char(created_at, 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') "
            "FROM folders "
            "WHERE owner_id = $1 AND parent_id IS NULL AND deleted_at IS NULL "
            "ORDER BY name ASC LIMIT $2",
            owner_id,
            limit)
        : txn.exec_params(
            "SELECT id::text, name, parent_id::text, owner_id::text, "
            "       to_char(created_at, 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"') "
            "FROM folders "
            "WHERE owner_id = $1 AND parent_id = $2 AND deleted_at IS NULL "
            "ORDER BY name ASC LIMIT $3",
            owner_id,
            parent_id,
            limit);

    std::vector<FolderRecord> records;
    records.reserve(result.size());
    for (const auto& row : result) {
        FolderRecord rec;
        rec.id = row[0].as<std::string>();
        rec.name = row[1].as<std::string>();
        if (!row[2].is_null()) {
            rec.parent_id = row[2].as<std::string>();
        }
        rec.owner_id = row[3].as<std::string>();
        rec.created_at = row[4].as<std::string>();
        records.push_back(std::move(rec));
    }
    return records;
}

bool FolderDao::name_exists(const std::string& owner_id,
                            const std::string& parent_id,
                            const std::string& name)
{
    auto guard = pool_.acquire();
    pqxx::work txn(*guard);

    pqxx::result result = parent_id.empty()
        ? txn.exec_params(
            "SELECT 1 FROM folders "
            "WHERE owner_id = $1 AND parent_id IS NULL AND name = $2 "
            "AND deleted_at IS NULL LIMIT 1",
            owner_id,
            name)
        : txn.exec_params(
            "SELECT 1 FROM folders "
            "WHERE owner_id = $1 AND parent_id = $2 AND name = $3 "
            "AND deleted_at IS NULL LIMIT 1",
            owner_id,
            parent_id,
            name);

    return !result.empty();
}

void FolderDao::soft_delete(const std::string& id)
{
    auto guard = pool_.acquire();
    pqxx::work txn(*guard);
    txn.exec_params("UPDATE folders SET deleted_at = NOW() WHERE id = $1", id);
    txn.commit();
}

bool FolderDao::has_children(const std::string& id)
{
    auto guard = pool_.acquire();
    pqxx::work txn(*guard);

    auto subfolders = txn.exec_params(
        "SELECT 1 FROM folders WHERE parent_id = $1 AND deleted_at IS NULL LIMIT 1",
        id
    );
    if (!subfolders.empty()) {
        return true;
    }

    auto files = txn.exec_params(
        "SELECT 1 FROM files WHERE folder_id = $1 AND deleted_at IS NULL LIMIT 1",
        id
    );
    return !files.empty();
}

bool FolderDao::rename(const std::string& id, const std::string& name)
{
    auto guard = pool_.acquire();
    pqxx::work txn(*guard);
    txn.exec_params(
        "UPDATE folders SET name = $2 WHERE id = $1 AND deleted_at IS NULL",
        id,
        name
    );
    txn.commit();
    return true;
}

} // namespace solar_metadata
