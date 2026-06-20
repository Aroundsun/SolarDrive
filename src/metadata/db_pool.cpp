// =============================================================================
// db_pool.cpp — PostgreSQL 连接池实现
// SolarDrive 元数据层：预创建连接、阻塞 acquire、Guard 析构归还
// =============================================================================
#include "db_pool.h"

#include <stdexcept>

namespace solar_metadata {

DbPool::DbPool(const std::string& conn_str, int pool_size)
    : conn_str_(conn_str)
{
    // 启动时预创建 pool_size 个连接
    for (int i = 0; i < pool_size; ++i) {
        auto conn = std::make_unique<pqxx::connection>(conn_str);
        pool_.push(std::move(conn));
    }
}

DbPool::~DbPool()
{
    // unique_ptr 会自动释放所有连接
    std::lock_guard<std::mutex> lock(mtx_);
    while (!pool_.empty()) {
        pool_.pop();
    }
}

DbPool::Guard DbPool::acquire()
{
    std::unique_lock<std::mutex> lock(mtx_);
    // 池空时阻塞，直到有连接被 release 归还
    cv_.wait(lock, [this]() { return !pool_.empty(); });

    auto conn = std::move(pool_.front());
    pool_.pop();
    lock.unlock();

    return Guard(std::move(conn), this);
}

void DbPool::release(std::unique_ptr<pqxx::connection> conn)
{
    {
        std::lock_guard<std::mutex> lock(mtx_);
        pool_.push(std::move(conn));
    }
    cv_.notify_one();
}

// ---- Guard ----

DbPool::Guard::Guard(std::unique_ptr<pqxx::connection>&& conn, DbPool* pool)
    : conn_(std::move(conn))
    , pool_(pool)
{
}

DbPool::Guard::~Guard()
{
    // 作用域结束时自动归还连接，避免泄漏
    if (conn_ && pool_) {
        pool_->release(std::move(conn_));
    }
}

} // namespace solar_metadata
