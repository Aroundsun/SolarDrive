// =============================================================================
// db_pool.h — PostgreSQL 连接池
// SolarDrive 元数据层：线程安全连接复用，RAII Guard 自动归还
// =============================================================================
#pragma once

#include <pqxx/pqxx>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <string>

namespace solar_metadata {

// PostgreSQL 连接池
// 线程安全，使用 RAII Guard 模式管理连接的获取和归还
class DbPool {
public:
    DbPool(const std::string& conn_str, int pool_size = 8);
    ~DbPool();

    // RAII guard - 获取连接，析构时自动归还
    class Guard {
    public:
        Guard(std::unique_ptr<pqxx::connection>&& conn, DbPool* pool);
        ~Guard();

        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;
        Guard(Guard&&) = default;
        Guard& operator=(Guard&&) = default;

        /// 透传 pqxx::connection 指针/引用，便于 txn(*guard) 用法
        pqxx::connection* operator->() { return conn_.get(); }
        pqxx::connection& operator*() { return *conn_; }

    private:
        std::unique_ptr<pqxx::connection> conn_;
        DbPool* pool_;
    };

    // 获取一个连接（阻塞直到有可用连接）
    Guard acquire();

private:
    /// 将连接放回队列并唤醒等待线程
    void release(std::unique_ptr<pqxx::connection> conn);

    std::queue<std::unique_ptr<pqxx::connection>> pool_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::string conn_str_;
};

} // namespace solar_metadata
