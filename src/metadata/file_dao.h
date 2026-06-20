// =============================================================================
// file_dao.h — 用户文件目录项（files）+ 内容对象（content_objects，JOIN 视图）
// =============================================================================
#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

#include "db_pool.h"
#include "content_dao.h"

namespace solar_metadata {

struct FileRecord {
    std::string id;          // files.id（目录项）
    std::string name;
    int64_t     size = 0;
    std::string hash;
    std::string chunk_hashes;
    std::string mime_type;
    std::string created_at;
    std::string owner_id;
    std::string folder_id;   // 空 = 根目录
    std::string content_id;  // content_objects.id
};

class FileDao {
public:
    explicit FileDao(DbPool& pool, ContentDao& content_dao);

    void create_table();

    // 写入内容对象（去重）+ 新建目录项
    std::string insert(const FileRecord& file);

    // 仅新建目录项，指向已有 content_id（秒传 / 转存）
    std::string insert_link(const FileRecord& file, const std::string& content_id);

    std::optional<FileRecord> find_by_id(const std::string& id);

    std::optional<FileRecord> find_owned_by_id(const std::string& owner_id,
                                               const std::string& id);

    std::optional<FileRecord> find_content_by_hash(const std::string& hash);

    void soft_delete(const std::string& id);

    std::vector<FileRecord> list_by_folder(const std::string& owner_id,
                                           const std::string& folder_id,
                                           int limit = 200);

    int count_in_folder(const std::string& folder_id);

    bool name_exists_in_folder(const std::string& owner_id,
                               const std::string& folder_id,
                               const std::string& name);

    std::string make_unique_name(const std::string& name,
                                 const std::string& owner_id,
                                 const std::string& folder_id);

private:
    static void require_owner_id(const FileRecord& file);

    DbPool&      pool_;
    ContentDao&  content_dao_;
};

} // namespace solar_metadata
