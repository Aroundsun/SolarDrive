#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

#include "db_pool.h"

namespace solar_metadata {

struct FolderRecord {
    std::string id;
    std::string name;
    std::string parent_id;   // 空表示根目录
    std::string owner_id;
    std::string created_at;
};

class FolderDao {
public:
    explicit FolderDao(DbPool& pool);

    void create_table();

    std::string insert(const FolderRecord& folder);

    std::optional<FolderRecord> find_by_id(const std::string& id);

    std::vector<FolderRecord> list_by_parent(const std::string& owner_id,
                                             const std::string& parent_id,
                                             int limit = 200);

    bool name_exists(const std::string& owner_id,
                     const std::string& parent_id,
                     const std::string& name);

    void soft_delete(const std::string& id);

    bool has_children(const std::string& id);

    bool rename(const std::string& id, const std::string& name);

private:
    DbPool& pool_;
};

} // namespace solar_metadata
