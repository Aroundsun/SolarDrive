#pragma once

#include "db_pool.h"

#include <optional>
#include <string>
#include <vector>

namespace solar_metadata {

/// 文件分享记录，对应 file_shares 表一行
struct ShareRecord {
    std::string id;
    std::string file_id;
    std::string owner_id;
    std::string share_token;
    std::string password_hash;   // 空字符串 = 无密码
    std::string expires_at;       // 空字符串 = 永不过期
    int         max_downloads;    // 0 = 不限制下载次数
    int         download_count;
    std::string created_at;
    bool        is_revoked;
};

/// 分享链接 DAO：创建、查询、撤销、下载计数
class ShareDao {
public:
    explicit ShareDao(DbPool& pool);

    void create_table();

    std::string insert(const ShareRecord& record);

    std::optional<ShareRecord> find_by_token(const std::string& token);

    std::vector<ShareRecord> list_by_owner(const std::string& owner_id,
                                           int limit = 20,
                                           int offset = 0);

    void increment_download_count(const std::string& id);

    void revoke(const std::string& id, const std::string& owner_id);

    /// 文件删除时撤销其全部有效分享
    void revoke_by_file_id(const std::string& file_id);

    /// 撤销已过期的分享，返回影响行数
    int revoke_expired();

    bool token_exists(const std::string& token);

private:
    DbPool& pool_;
};

} // namespace solar_metadata
