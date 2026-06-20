// =============================================================================
// share_dao.h — 文件分享数据访问层
// SolarDrive 元数据层：file_shares 表 CRUD、token 校验、下载计数
// =============================================================================
#pragma once

#include "../metadata/db_pool.h"
#include <string>
#include <optional>
#include <vector>

namespace solar_api {

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
    explicit ShareDao(solar_metadata::DbPool& pool);
    /// 建表及 token/owner 索引
    void create_table();

    // 插入分享记录，返回 id
    std::string insert(const ShareRecord& record);

    // 通过 token 查询（验证用）
    std::optional<ShareRecord> find_by_token(const std::string& token);

    // 查询用户的分享列表（按时间倒序）
    std::vector<ShareRecord> list_by_owner(const std::string& owner_id, int limit = 20, int offset = 0);

    // 增加下载计数
    void increment_download_count(const std::string& id);

    // 撤销分享
    void revoke(const std::string& id, const std::string& owner_id);

    // 检查 token 是否已存在（生成时去重）
    bool token_exists(const std::string& token);

private:
    solar_metadata::DbPool& pool_;
};

} // namespace solar_api
