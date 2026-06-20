// ---------------------------------------------------------------------------
// user_dao.cpp
//
// 用户 DAO 实现：密码哈希、用户注册/登录、存储用量更新。
// ---------------------------------------------------------------------------

#include "user_dao.h"
#include <pqxx/pqxx>
#include <pqxx/except>
#include <openssl/evp.h>
#include <iomanip>
#include <sstream>

namespace solar_auth {

// 对明文密码做 SHA-256 哈希，输出十六进制字符串
std::string UserDao::sha256(const std::string& input) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int  digest_len = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, input.data(), input.size());
    EVP_DigestFinal_ex(ctx, digest, &digest_len);
    EVP_MD_CTX_free(ctx);

    std::ostringstream oss;
    for (unsigned int i = 0; i < digest_len; i++)
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)digest[i];
    return oss.str();
}

UserDao::UserDao(solar_metadata::DbPool& pool) : pool_(pool) {}

void UserDao::create_table() {
    // Schema 由 bootstrap_schema() 统一管理
}

std::optional<std::string> UserDao::register_user(const std::string& username,
                                                   const std::string& password) {
    std::string hash = sha256(password);
    auto g = pool_.acquire();
    pqxx::work tx(*g);
    try {
        auto r = tx.exec_params1(
            "INSERT INTO users(username, password_hash) VALUES($1, $2) RETURNING id::text",
            username, hash
        );
        const std::string id = r[0].as<std::string>();
        tx.commit();
        return id;
    } catch (const pqxx::unique_violation&) {
        return std::nullopt;
    }
}

std::optional<UserRecord> UserDao::login(const std::string& username, const std::string& password) {
    // 将输入密码哈希后与库中 password_hash 比对
    std::string hash = sha256(password);
    auto g = pool_.acquire();
    pqxx::work tx(*g);
    auto r = tx.exec_params(
        "SELECT id, username, password_hash, storage_quota, used_storage, created_at "
        "FROM users WHERE username=$1 AND password_hash=$2",
        username, hash
    );
    if (r.empty()) return std::nullopt;

    auto row = r[0];
    UserRecord u;
    u.id            = row["id"].as<std::string>();
    u.username      = row["username"].as<std::string>();
    u.password_hash = row["password_hash"].as<std::string>();
    u.storage_quota = row["storage_quota"].as<int64_t>();
    u.used_storage  = row["used_storage"].as<int64_t>();
    u.created_at    = row["created_at"].as<std::string>();
    return u;
}

std::optional<UserRecord> UserDao::find_by_id(const std::string& id) {
    auto g = pool_.acquire();
    pqxx::work tx(*g);
    auto r = tx.exec_params(
        "SELECT id, username, password_hash, storage_quota, used_storage, created_at "
        "FROM users WHERE id=$1",
        id
    );
    if (r.empty()) return std::nullopt;

    auto row = r[0];
    UserRecord u;
    u.id            = row["id"].as<std::string>();
    u.username      = row["username"].as<std::string>();
    u.password_hash = row["password_hash"].as<std::string>();
    u.storage_quota = row["storage_quota"].as<int64_t>();
    u.used_storage  = row["used_storage"].as<int64_t>();
    u.created_at    = row["created_at"].as<std::string>();
    return u;
}

void UserDao::update_used_storage(const std::string& user_id, int64_t delta) {
    auto g = pool_.acquire();
    pqxx::work tx(*g);
    // 原子增量更新，避免并发上传/删除时的竞态
    tx.exec_params(
        "UPDATE users SET used_storage = used_storage + $1 WHERE id = $2",
        delta, user_id
    );
    tx.commit();
}

} // namespace solar_auth
