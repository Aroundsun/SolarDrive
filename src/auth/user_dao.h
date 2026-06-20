#pragma once

#include "../metadata/db_pool.h"
#include <string>
#include <optional>

namespace solar_auth {

struct UserRecord {
    std::string id;
    std::string username;
    std::string password_hash;
    int64_t     storage_quota;    // 配额（字节），默认 10GB
    int64_t     used_storage;     // 已用（字节）
    std::string created_at;
};

class UserDao {
public:
    explicit UserDao(solar_metadata::DbPool& pool);
    void create_table();

    // 注册（密码用 SHA-256，开发用；TODO: 上线换成 bcrypt）
    std::string register_user(const std::string& username, const std::string& password);

    // 登录验证
    std::optional<UserRecord> login(const std::string& username, const std::string& password);

    std::optional<UserRecord> find_by_id(const std::string& id);

    // 更新已用存储空间
    void update_used_storage(const std::string& user_id, int64_t delta);

private:
    static std::string sha256(const std::string& input);
    solar_metadata::DbPool& pool_;
};

} // namespace solar_auth
