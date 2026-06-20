#include "redis_client.h"
#include <cstring>
#include <iostream>

namespace solar_cache {

RedisClient::RedisClient(const std::string& host, int port) {
    ctx_ = redisConnect(host.c_str(), port);
    if (!ctx_ || ctx_->err) {
        std::cerr << "[redis] connection error: "
                  << (ctx_ ? ctx_->errstr : "can't allocate context")
                  << "\n";
        if (ctx_) {
            redisFree(ctx_);
            ctx_ = nullptr;
        }
    }
}

RedisClient::~RedisClient() {
    disconnect();
}

void RedisClient::disconnect() {
    if (ctx_) {
        redisFree(ctx_);
        ctx_ = nullptr;
    }
}

bool RedisClient::ping() {
    if (!ctx_) return false;
    redisReply* reply = static_cast<redisReply*>(redisCommand(ctx_, "PING"));
    bool ok = (reply && reply->type == REDIS_REPLY_STATUS &&
               std::strcmp(reply->str, "PONG") == 0);
    if (reply) freeReplyObject(reply);
    return ok;
}

bool RedisClient::set(const std::string& key, const std::string& value, int ttl_seconds) {
    if (!ctx_) return false;
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "SET %s %s", key.c_str(), value.c_str()));
    bool ok = (reply && reply->type == REDIS_REPLY_STATUS);
    if (ok && ttl_seconds > 0) {
        freeReplyObject(reply);
        reply = static_cast<redisReply*>(
            redisCommand(ctx_, "EXPIRE %s %d", key.c_str(), ttl_seconds));
        ok = (reply && reply->type == REDIS_REPLY_INTEGER && reply->integer == 1);
    }
    if (reply) freeReplyObject(reply);
    return ok;
}

std::optional<std::string> RedisClient::get(const std::string& key) {
    if (!ctx_) return std::nullopt;
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "GET %s", key.c_str()));
    if (!reply) return std::nullopt;
    if (reply->type == REDIS_REPLY_STRING) {
        std::string val(reply->str, reply->len);
        freeReplyObject(reply);
        return val;
    }
    freeReplyObject(reply);
    return std::nullopt;
}

bool RedisClient::del(const std::string& key) {
    if (!ctx_) return false;
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "DEL %s", key.c_str()));
    bool ok = (reply && reply->type == REDIS_REPLY_INTEGER);
    if (reply) freeReplyObject(reply);
    return ok;
}

bool RedisClient::sadd(const std::string& key, const std::string& member) {
    if (!ctx_) return false;
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "SADD %s %s", key.c_str(), member.c_str()));
    bool ok = (reply && reply->type == REDIS_REPLY_INTEGER);
    if (reply) freeReplyObject(reply);
    return ok;
}

bool RedisClient::sismember(const std::string& key, const std::string& member) {
    if (!ctx_) return false;
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "SISMEMBER %s %s", key.c_str(), member.c_str()));
    bool ok = (reply && reply->type == REDIS_REPLY_INTEGER && reply->integer == 1);
    if (reply) freeReplyObject(reply);
    return ok;
}

bool RedisClient::hset(const std::string& key, const std::string& field, const std::string& value) {
    if (!ctx_) return false;
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "HSET %s %s %s", key.c_str(), field.c_str(), value.c_str()));
    bool ok = (reply && reply->type == REDIS_REPLY_INTEGER);
    if (reply) freeReplyObject(reply);
    return ok;
}

std::optional<std::string> RedisClient::hget(const std::string& key, const std::string& field) {
    if (!ctx_) return std::nullopt;
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "HGET %s %s", key.c_str(), field.c_str()));
    if (!reply) return std::nullopt;
    if (reply->type == REDIS_REPLY_STRING) {
        std::string val(reply->str, reply->len);
        freeReplyObject(reply);
        return val;
    }
    freeReplyObject(reply);
    return std::nullopt;
}

int64_t RedisClient::incr(const std::string& key, int ttl_seconds) {
    if (!ctx_) return -1;
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "INCR %s", key.c_str()));
    if (!reply || reply->type != REDIS_REPLY_INTEGER) {
        if (reply) freeReplyObject(reply);
        return -1;
    }
    int64_t val = static_cast<int64_t>(reply->integer);
    freeReplyObject(reply);

    // 第一次创建时设置 TTL
    if (val == 1 && ttl_seconds > 0) {
        reply = static_cast<redisReply*>(
            redisCommand(ctx_, "EXPIRE %s %d", key.c_str(), ttl_seconds));
        if (reply) freeReplyObject(reply);
    }
    return val;
}

} // namespace solar_cache
