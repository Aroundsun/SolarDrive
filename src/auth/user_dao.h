// ---------------------------------------------------------------------------
// user_dao.h
//
// 用户数据访问层：users 表的 CRUD，负责注册、登录验证与存储配额管理。
// ---------------------------------------------------------------------------

#pragma once

#include "../metadata/db_pool.h"
#include <string>
#include <optional>

namespace solar_auth {

// 用户数据库记录
struct UserRecord {
    std::string id;
    std::string username;
    std::string password_hash;
    int64_t     storage_quota;    // 存储配额（字节），默认 10GB
    int64_t     used_storage;     // 已用存储（字节）
    std::string created_at;
};

// 用户 DAO：封装 PostgreSQL users 表操作
class UserDao {
public:
    explicit UserDao(solar_metadata::DbPool& pool);

    // 创建 users 表及 username 索引（幂等）
    void create_table();

    // 注册新用户，密码经 SHA-256 哈希后入库（TODO: 上线换 bcrypt）
    // 用户名冲突时返回 nullopt
    std::optional<std::string> register_user(const std::string& username,
                                             const std::string& password);

    // 登录验证：用户名 + 密码匹配则返回 UserRecord
    std::optional<UserRecord> login(const std::string& username, const std::string& password);

    // 按用户 ID 查询
    std::optional<UserRecord> find_by_id(const std::string& id);

    // 增量更新已用存储空间（上传 +delta，删除 -delta）
    void update_used_storage(const std::string& user_id, int64_t delta);

private:
    static std::string sha256(const std::string& input);
    solar_metadata::DbPool& pool_;
};

} // namespace solar_auth
