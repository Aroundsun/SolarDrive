// share_handler.cpp — 文件分享 API 实现
// 分享创建/列表/撤销、公开下载（含页面重定向）、转存副本到当前用户网盘

#include "share_handler.h"
#include "../auth/auth_middleware.h"
#include "../auth/access_control.h"
#include <nlohmann/json.hpp>
#include <random>
#include <ctime>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <cstdlib>

using json = nlohmann::json;

namespace solar_api {

namespace {

// 分享落地页路径（前端 share.html）
std::string share_page_path(const std::string& token) {
    return "/share.html?token=" + token;
}

// 前端 API 发起的实际下载请求（带自定义头，不走页面重定向）
bool is_share_api_download(const solar_http::HttpRequest& req) {
    return req.get_header("X-Share-Download") == "1";
}

// 在线预览请求：inline 展示，不计入下载次数
bool is_share_api_preview(const solar_http::HttpRequest& req) {
    return req.get_header("X-Share-Preview") == "1";
}

// 浏览器直接访问 /s/{token} 时重定向到分享页，API 请求则返回文件流
bool wants_share_page_redirect(const solar_http::HttpRequest& req) {
    if (is_share_api_download(req) || is_share_api_preview(req)) {
        return false;
    }
    const std::string accept = req.get_header("Accept");
    if (accept.find("text/html") != std::string::npos) {
        return true;
    }
  // 浏览器直接访问 /s/{token} 时 Accept 常为 */*
    return accept.empty() || accept == "*/*";
}

} // namespace

ShareHandler::ShareHandler(
    std::shared_ptr<ShareDao> share_dao,
    solar_metadata::FileDao& file_dao,
    solar_storage::ObjectStore& store,
    std::shared_ptr<solar_cache::RedisClient> redis)
    : share_dao_(std::move(share_dao))
    , file_dao_(file_dao)
    , store_(store)
    , redis_(std::move(redis)) {}

// 生成 8 位随机 token，如果冲突则重试
std::string ShareHandler::generate_token() {
    static const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    static std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 35);

    for (int attempt = 0; attempt < 10; attempt++) {
        std::string token(8, ' ');
        for (auto& c : token) c = chars[dist(rng)];
        if (!share_dao_->token_exists(token))
            return token;
    }
    // 极端情况：10 次都冲突（概率极低），抛异常
    throw std::runtime_error("failed to generate unique share token");
}

// 分享访问统一校验入口：查库 → 检查撤销/过期/密码/下载上限 → 可选递增计数
std::optional<ShareRecord> ShareHandler::resolve_share(
    const std::string& token,
    const std::string& password,
    solar_http::HttpResponse& resp,
    bool count_download) {
    if (token.empty()) {
        resp.set_error(400, "missing share token");
        return std::nullopt;
    }

    auto share = share_dao_->find_by_token(token);
    if (!share) {
        resp.set_error(404, "share not found");
        return std::nullopt;
    }

    if (share->is_revoked) {
        resp.set_error(403, "share has been revoked");
        return std::nullopt;
    }

    // 解析 ISO 8601 或 "YYYY-MM-DD HH:MM:SS" 格式的过期时间
    if (!share->expires_at.empty()) {
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm tm{};
        std::istringstream iss(share->expires_at);
        iss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
        if (iss.fail()) {
            iss.clear();
            iss.str(share->expires_at);
            iss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
        }
        if (!iss.fail()) {
            auto exp_t = timegm(&tm);
            if (now > exp_t) {
                resp.set_error(410, "share has expired");
                return std::nullopt;
            }
        }
    }

    if (!share->password_hash.empty()) {
        if (password.empty()) {
            resp.set_error(401, "password required");
            resp.set_header("X-Share-Auth", "required");
            return std::nullopt;
        }
        std::string pwd_hash = solar_storage::ObjectStore::sha256(password);
        if (pwd_hash != share->password_hash) {
            resp.set_error(403, "incorrect password");
            return std::nullopt;
        }
    }

    if (count_download &&
        share->max_downloads > 0 &&
        share->download_count >= share->max_downloads) {
        resp.set_error(403, "download limit reached");
        return std::nullopt;
    }

    // 实际下载时递增 DB 计数，并同步更新 Redis 缓存中的 download_count
    if (count_download) {
        share_dao_->increment_download_count(share->id);

        if (redis_) {
            auto cached = redis_->get("share:" + token);
            if (cached) {
                try {
                    json info = json::parse(*cached);
                    info["download_count"] = share->download_count + 1;
                    redis_->set("share:" + token, info.dump());
                } catch (...) {}
            }
        }
        share->download_count += 1;
    }

    return share;
}

// -------- 创建分享 --------

void ShareHandler::handle_create(const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
    try {
        if (!req.has_auth_user()) {
            resp.set_error(401, "Unauthorized");
            return;
        }
        const std::string& user_id = req.auth_user_id();

        json body = json::parse(req.body());
        std::string file_id   = body["file_id"].get<std::string>();
        int expires_in_hours  = body.value("expires_in_hours", 0);
        std::string password  = body.value("password", "");
        int max_downloads     = body.value("max_downloads", 0);

        auto file = solar_auth::require_owned_file(user_id, file_id, file_dao_, resp);
        if (!file) {
            return;
        }
        (void)file;

        // 生成 token
        std::string token = generate_token();

        // 构建记录
        ShareRecord record;
        record.file_id     = file_id;
        record.owner_id    = user_id;
        record.share_token = token;
        record.max_downloads = max_downloads;

        // 密码
        if (!password.empty()) {
            // 密码以 SHA-256 哈希存储，客户端提交明文后服务端比对
            record.password_hash = solar_storage::ObjectStore::sha256(password);
        }

        // 过期时间
        if (expires_in_hours > 0) {
            // 格式：YYYY-MM-DD HH:MI:SS+08
            auto now = std::chrono::system_clock::now();
            auto exp = now + std::chrono::hours(expires_in_hours);
            auto exp_t = std::chrono::system_clock::to_time_t(exp);
            std::tm tm{};
            localtime_r(&exp_t, &tm);
            std::ostringstream oss;
            oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S%z");
            record.expires_at = oss.str();
            // 插入 + 号（put_time 的 %z 不输出 + 号前的内容）
            // 简单处理：用 ISO 8601 格式
            std::ostringstream iso;
            iso << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
            // 计算时区偏移
            int tz_offset = tm.tm_gmtoff / 3600;
            iso << (tz_offset >= 0 ? "+" : "-")
                << (tz_offset < 10 ? "0" : "") << std::abs(tz_offset)
                << ":00";
            record.expires_at = iso.str();
        }

        share_dao_->insert(record);

        // 写入 Redis 缓存
        if (redis_) {
            json share_info;
            share_info["file_id"] = file_id;
            share_info["password_hash"] = record.password_hash;
            share_info["max_downloads"] = max_downloads;
            share_info["download_count"] = 0;
            share_info["expires_at"] = record.expires_at;
            share_info["is_revoked"] = false;
            redis_->set("share:" + token, share_info.dump(),
                        expires_in_hours > 0 ? expires_in_hours * 3600 : 86400);
        }

        json j;
        j["share_token"] = token;
        j["url"]         = share_page_path(token);
        j["download_url"] = "/s/" + token;
        j["expires_at"]  = record.expires_at.empty() ? nullptr : record.expires_at;
        j["has_password"] = !password.empty();
        resp.set_json(j.dump());

    } catch (const json::exception& e) {
        resp.set_error(400, std::string("invalid JSON: ") + e.what());
    } catch (const std::exception& e) {
        resp.set_error(500, std::string("share failed: ") + e.what());
    }
}

// -------- 列表 --------

void ShareHandler::handle_list(const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
    try {
        if (!req.has_auth_user()) {
            resp.set_error(401, "Unauthorized");
            return;
        }

        auto shares = share_dao_->list_by_owner(req.auth_user_id());

        json arr = json::array();
        for (auto& s : shares) {
            json item;
            item["share_token"]    = s.share_token;
            item["file_id"]        = s.file_id;
            item["url"]            = share_page_path(s.share_token);
            item["download_url"]   = "/s/" + s.share_token;
            item["download_count"] = s.download_count;
            item["max_downloads"]  = s.max_downloads;
            item["has_password"]   = !s.password_hash.empty();
            item["created_at"]     = s.created_at;
            item["expires_at"]     = s.expires_at.empty() ? nullptr : s.expires_at;
            arr.push_back(item);
        }
        resp.set_json(arr.dump());
    } catch (const std::exception& e) {
        resp.set_error(500, std::string("list shares failed: ") + e.what());
    }
}

// -------- 撤销 --------

void ShareHandler::handle_revoke(const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
    try {
        if (!req.has_auth_user()) {
            resp.set_error(401, "Unauthorized");
            return;
        }

        std::string token = req.get_path_param("token");
        if (token.empty()) {
            resp.set_error(400, "missing token");
            return;
        }

        auto share = share_dao_->find_by_token(token);
        if (!share) {
            resp.set_error(404, "share not found");
            return;
        }
        if (share->owner_id != req.auth_user_id()) {
            resp.set_error(403, "not your share");
            return;
        }

        share_dao_->revoke(share->id, req.auth_user_id());

        // 删 Redis 缓存
        if (redis_) redis_->del("share:" + token);

        resp.set_json(R"({"revoked":true})");
    } catch (const std::exception& e) {
        resp.set_error(500, std::string("revoke failed: ") + e.what());
    }
}

// -------- 公开下载 --------

void ShareHandler::handle_download(const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
    try {
        std::string token = req.get_path_param("token");
        if (token.empty()) {
            resp.set_error(400, "missing share token");
            return;
        }

        // 浏览器访问：302 跳转到分享页，由前端处理密码与下载
        if (wants_share_page_redirect(req)) {
            resp.set_status(302, "Found");
            resp.set_header("Location", share_page_path(token));
            resp.set_body("");
            resp.headers_["Content-Length"] = "0";
            return;
        }

        std::string pwd;
        auto q = req.query();
        auto pos = q.find("password=");
        if (pos != std::string::npos) {
            pwd = q.substr(pos + 9);
        }
        if (pwd.empty()) {
            pwd = req.get_header("X-Share-Password");
        }

        const bool is_preview  = is_share_api_preview(req);
        const bool is_download = is_share_api_download(req);
        // 预览不计入下载次数，仅 attachment 下载才计数
        const bool count_download = is_download && !is_preview;

        auto share = resolve_share(token, pwd, resp, count_download);
        if (!share) {
            return;
        }

        auto file = file_dao_.find_by_id(share->file_id);
        if (!file) {
            resp.set_error(404, "original file not found");
            return;
        }

        json chunk_arr = json::parse(file->chunk_hashes);
        std::vector<std::string> hashes = chunk_arr.get<std::vector<std::string>>();
        std::string content = store_.get_chunked(hashes);

        const char* disposition = is_preview ? "inline" : "attachment";
        resp.headers_["Content-Disposition"] =
            std::string(disposition) + "; filename=\"" + file->name + "\"";
        resp.set_binary(content, file->mime_type);

    } catch (const std::exception& e) {
        resp.set_error(500, std::string("download failed: ") + e.what());
    }
}

// -------- 保存分享文件到个人网盘 --------

// 校验分享后复制元数据（共享底层对象，不重复存储内容）
void ShareHandler::handle_save(const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
    try {
        if (!req.has_auth_user()) {
            resp.set_error(401, "Unauthorized");
            return;
        }
        const std::string& user_id = req.auth_user_id();

        json body = json::parse(req.body());
        std::string token = body.value("share_token", "");
        if (token.empty()) {
            token = body.value("token", "");
        }
        std::string password = body.value("password", "");

        auto share = resolve_share(token, password, resp, false);
        if (!share) {
            return;
        }

        auto file = file_dao_.find_by_id(share->file_id);
        if (!file) {
            resp.set_error(404, "original file not found");
            return;
        }

        // 转存：复用原 content_id，仅新建目录项
        solar_metadata::FileRecord dup;
        dup.name      = file_dao_.make_unique_name(file->name, user_id, "");
        dup.owner_id  = user_id;
        dup.mime_type = file->mime_type;

        std::string new_id = file_dao_.insert_link(dup, file->content_id);

        json j;
        j["file_id"] = new_id;
        j["name"]    = dup.name;
        j["size"]    = file->size;
        j["saved"]   = true;
        resp.set_json(j.dump());
    } catch (const json::exception& e) {
        resp.set_error(400, std::string("invalid JSON: ") + e.what());
    } catch (const std::exception& e) {
        resp.set_error(500, std::string("save failed: ") + e.what());
    }
}

} // namespace solar_api
