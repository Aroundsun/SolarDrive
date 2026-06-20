#include "user_dao.h"
#include <pqxx/pqxx>
#include <openssl/evp.h>
#include <iomanip>
#include <sstream>

namespace solar_auth {

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
    auto g = pool_.acquire();
    pqxx::work tx(*g);
    tx.exec(R"(
        CREATE TABLE IF NOT EXISTS users (
            id            UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            username      TEXT UNIQUE NOT NULL,
            password_hash TEXT NOT NULL,
            storage_quota BIGINT NOT NULL DEFAULT 10737418240,
            used_storage  BIGINT NOT NULL DEFAULT 0,
            created_at    TIMESTAMPTZ NOT NULL DEFAULT NOW()
        )
    )");
    tx.exec("CREATE INDEX IF NOT EXISTS idx_users_username ON users(username)");
    tx.commit();
}

std::string UserDao::register_user(const std::string& username, const std::string& password) {
    std::string hash = sha256(password);
    auto g = pool_.acquire();
    pqxx::work tx(*g);
    auto r = tx.exec_params1(
        "INSERT INTO users(username, password_hash) VALUES($1, $2) RETURNING id::text",
        username, hash
    );
    tx.commit();
    return r[0].as<std::string>();
}

std::optional<UserRecord> UserDao::login(const std::string& username, const std::string& password) {
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
    tx.exec_params(
        "UPDATE users SET used_storage = used_storage + $1 WHERE id = $2",
        delta, user_id
    );
    tx.commit();
}

} // namespace solar_auth
