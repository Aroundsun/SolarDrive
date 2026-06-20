// multipart_upload_handler.cpp — 分片上传 API 实现
// 会话存 Redis，分片写入对象存储，完成后合并元数据并建立秒传索引

#include "multipart_upload_handler.h"
#include "file_upload_ops.h"
#include "../auth/access_control.h"
#include "upload_limits.h"
#include "../ws/ws_session.h"
#include <nlohmann/json.hpp>
#include <ctime>
#include <sstream>
#include <random>

using json = nlohmann::json;

namespace solar_api {

namespace {

// 根据已上传分片数计算进度，经 WebSocket 推送给前端
void notify_upload_progress(const MultipartSession& session) {
    int uploaded_count = 0;
    for (bool uploaded : session.uploaded) {
        if (uploaded) {
            ++uploaded_count;
        }
    }

    const int percent = session.chunk_count > 0
        ? (uploaded_count * 100 / session.chunk_count)
        : 0;

    int64_t bytes_uploaded = static_cast<int64_t>(uploaded_count) * session.chunk_size;
    if (bytes_uploaded > session.total_size) {
        bytes_uploaded = session.total_size;
    }

    solar_ws::WsSessionManager::instance().push_progress(
        session.upload_id, percent, bytes_uploaded, session.total_size);
}

} // namespace

// 生成 upload_id（伪 UUID，四段十六进制随机数）
static std::string generate_uuid() {
    static std::mt19937_64 rng(std::random_device{}());
    std::ostringstream oss;
    oss << std::hex;
    for (int i = 0; i < 4; i++) {
        uint64_t v = rng();
        oss << v;
        if (i < 3) oss << "-";
    }
    return oss.str();
}

MultipartUploadHandler::MultipartUploadHandler(
    solar_storage::ObjectStore& store,
    solar_metadata::FileDao& dao,
    solar_metadata::FolderDao& folder_dao,
    std::shared_ptr<solar_cache::RedisClient> redis,
    size_t chunk_size,
    int64_t max_file_size_bytes)
    : store_(store)
    , dao_(dao)
    , folder_dao_(folder_dao)
    , redis_(std::move(redis))
    , chunk_size_(chunk_size > 0 ? chunk_size : store.chunk_size())
    , max_file_size_bytes_(max_file_size_bytes) {}

// -------- 初始化上传会话 --------

// 根据文件大小计算分片数（默认 4MB/片），在 Redis 创建会话
void MultipartUploadHandler::handle_init(
    const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
    try {
        if (!req.has_auth_user()) {
            resp.set_error(401, "Unauthorized");
            return;
        }
        const std::string& user_id = req.auth_user_id();

        json body = json::parse(req.body());
        std::string file_name = body["file_name"].get<std::string>();
        int64_t total_size    = body["total_size"].get<int64_t>();
        std::string mime_type = body.value("mime_type", "application/octet-stream");
        std::string folder_id;
        if (body.contains("folder_id") && !body["folder_id"].is_null()) {
            folder_id = body["folder_id"].get<std::string>();
        }

        if (file_name.empty() || total_size <= 0) {
            resp.set_error(400, "file_name and total_size are required");
            return;
        }
        if (reject_oversized(total_size, max_file_size_bytes_, resp)) {
            return;
        }

        auto resolved_folder = solar_auth::resolve_folder_id(
            folder_id, user_id, folder_dao_, resp);
        if (!resolved_folder) {
            return;
        }

        MultipartSession session;
        session.upload_id    = generate_uuid();
        session.user_id      = user_id;
        session.folder_id    = *resolved_folder;
        session.file_name    = file_name;
        session.mime_type    = mime_type;
        session.total_size   = total_size;
        session.chunk_size   = static_cast<int64_t>(chunk_size_);
        session.chunk_count  = static_cast<int>(
            (total_size + session.chunk_size - 1) / session.chunk_size);
        session.uploaded.assign(session.chunk_count, false);
        session.part_hashes.assign(session.chunk_count, "");

        save_to_redis(session);

        json j;
        j["upload_id"]   = session.upload_id;
        j["chunk_size"]  = session.chunk_size;
        j["chunk_count"] = session.chunk_count;
        resp.set_json(j.dump());
    } catch (const json::exception& e) {
        resp.set_error(400, std::string("invalid JSON: ") + e.what());
    } catch (const std::exception& e) {
        resp.set_error(500, std::string("init failed: ") + e.what());
    }
}

// -------- 上传单个分片 --------

// 将分片 body 存入对象存储，更新会话中对应 part 的 hash 与 uploaded 标记
void MultipartUploadHandler::handle_upload_part(
    const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
    try {
        if (!req.has_auth_user()) {
            resp.set_error(401, "Unauthorized");
            return;
        }

        std::string upload_id = req.get_path_param("upload_id");
        std::string part_str  = req.get_path_param("part_num");

        if (upload_id.empty() || part_str.empty()) {
            resp.set_error(400, "missing upload_id or part_num");
            return;
        }

        int part_num = std::stoi(part_str);
        if (req.body().empty()) {
            resp.set_error(400, "empty part body");
            return;
        }

        MultipartSession session = load_from_redis(upload_id);
        if (session.upload_id.empty()) {
            resp.set_error(404, "upload session not found");
            return;
        }
        if (session.user_id != req.auth_user_id()) {
            resp.set_error(403, "forbidden");
            return;
        }
        if (part_num < 0 || part_num >= session.chunk_count) {
            resp.set_error(400, "invalid part_num");
            return;
        }
        if (static_cast<int64_t>(req.body().size()) > session.chunk_size) {
            resp.set_error(413, "part too large");
            return;
        }

        // 存储分片数据到对象存储
        std::string hash = store_.put(req.body());
        session.uploaded[part_num] = true;
        session.part_hashes[part_num] = hash;
        save_to_redis(session);
        notify_upload_progress(session);

        json j;
        j["part_num"] = part_num;
        j["hash"]     = hash;
        j["size"]     = req.body().size();
        resp.set_json(j.dump());
    } catch (const std::exception& e) {
        resp.set_error(500, std::string("upload part failed: ") + e.what());
    }
}

// -------- 完成上传 --------

// 校验全部分片已上传，合并计算全文哈希，写入 DB 并清理 Redis 会话
void MultipartUploadHandler::handle_complete(
    const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
    try {
        if (!req.has_auth_user()) {
            resp.set_error(401, "Unauthorized");
            return;
        }

        std::string upload_id = req.get_path_param("upload_id");
        if (upload_id.empty()) {
            resp.set_error(400, "missing upload_id");
            return;
        }

        MultipartSession session = load_from_redis(upload_id);
        if (session.upload_id.empty()) {
            resp.set_error(404, "upload session not found");
            return;
        }
        if (session.user_id != req.auth_user_id()) {
            resp.set_error(403, "forbidden");
            return;
        }

        // 检查是否所有分片都上传完成
        for (int i = 0; i < session.chunk_count; i++) {
            if (!session.uploaded[i]) {
                resp.set_error(400, "part " + std::to_string(i) + " not uploaded yet");
                return;
            }
        }

        // 构建 FileRecord
        solar_metadata::FileRecord f;
        f.name       = session.file_name;
        f.size       = session.total_size;
        f.mime_type  = session.mime_type;
        f.owner_id   = session.user_id;
        f.folder_id  = session.folder_id;

        // chunk_hashes 存为 JSON 数组字符串
        json chunk_arr = json::array();
        for (auto& h : session.part_hashes)
            chunk_arr.push_back(h);
        f.chunk_hashes = chunk_arr.dump();

        // 计算全文 SHA-256：读取所有分片合并后算（TODO: 优化为增量 hash）
        std::string full_content;
        for (auto& h : session.part_hashes)
            full_content += store_.get(h);
        f.hash = solar_storage::ObjectStore::sha256(full_content);

        std::string file_id;
        bool instant = false;

        if (auto content = dao_.find_content_by_hash(f.hash)) {
            file_id = link_existing_content(
                dao_, *content, session.file_name, session.mime_type,
                session.user_id, session.folder_id);
            instant = true;
            if (redis_) {
                redis_->set("file:hash:" + f.hash, content->content_id);
            }
        } else {
            file_id = dao_.insert(f);
            if (redis_) {
                if (auto content = dao_.find_content_by_hash(f.hash)) {
                    redis_->set("file:hash:" + f.hash, content->content_id);
                }
            }
        }

        delete_from_redis(upload_id);

        solar_ws::WsSessionManager::instance().push_complete(upload_id, file_id);

        json j;
        j["file_id"] = file_id;
        j["hash"]    = f.hash;
        j["size"]    = f.size;
        j["instant"] = instant;
        resp.set_json(j.dump());
    } catch (const std::exception& e) {
        resp.set_error(500, std::string("complete failed: ") + e.what());
    }
}

// -------- 查询上传进度（断点续传） --------

void MultipartUploadHandler::handle_status(
    const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
    try {
        if (!req.has_auth_user()) {
            resp.set_error(401, "Unauthorized");
            return;
        }

        std::string upload_id = req.get_path_param("upload_id");
        if (upload_id.empty()) {
            resp.set_error(400, "missing upload_id");
            return;
        }

        MultipartSession session = load_from_redis(upload_id);
        if (session.upload_id.empty()) {
            resp.set_error(404, "upload session not found");
            return;
        }
        if (session.user_id != req.auth_user_id()) {
            resp.set_error(403, "forbidden");
            return;
        }

        json j;
        j["upload_id"]  = session.upload_id;
        j["file_name"]  = session.file_name;
        j["total_size"] = session.total_size;
        j["chunk_size"] = session.chunk_size;
        j["chunk_count"] = session.chunk_count;

        json uploaded_arr = json::array();
        for (auto& h : session.part_hashes) {
            if (h.empty())
                uploaded_arr.push_back(nullptr);
            else
                uploaded_arr.push_back(h);
        }
        j["part_hashes"] = uploaded_arr;

        int uploaded_count = 0;
        for (auto u : session.uploaded)
            if (u) uploaded_count++;
        j["uploaded_count"] = uploaded_count;

        resp.set_json(j.dump());
    } catch (const std::exception& e) {
        resp.set_error(500, std::string("status failed: ") + e.what());
    }
}

// -------- 取消上传 --------

// 仅删除 Redis 会话，已上传的分片对象不主动清理
void MultipartUploadHandler::handle_abort(
    const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
    try {
        if (!req.has_auth_user()) {
            resp.set_error(401, "Unauthorized");
            return;
        }

        std::string upload_id = req.get_path_param("upload_id");
        if (upload_id.empty()) {
            resp.set_error(400, "missing upload_id");
            return;
        }

        MultipartSession session = load_from_redis(upload_id);
        if (session.upload_id.empty()) {
            resp.set_error(404, "upload session not found");
            return;
        }
        if (session.user_id != req.auth_user_id()) {
            resp.set_error(403, "forbidden");
            return;
        }

        delete_from_redis(upload_id);
        resp.set_json(R"({"status":"aborted"})");
    } catch (const std::exception& e) {
        resp.set_error(500, std::string("abort failed: ") + e.what());
    }
}

// -------- Redis 会话读写 --------

void MultipartUploadHandler::save_to_redis(const MultipartSession& session) {
    if (!redis_) return;
    std::string key = "multipart:" + session.upload_id;

    json part_hashes_arr = json::array();
    for (auto& h : session.part_hashes)
        part_hashes_arr.push_back(h);

    json uploaded_arr = json::array();
    for (auto u : session.uploaded)
        uploaded_arr.push_back(u);

    redis_->hset(key, "file_name",   session.file_name);
    redis_->hset(key, "mime_type",   session.mime_type);
    redis_->hset(key, "user_id",     session.user_id);
    redis_->hset(key, "folder_id",   session.folder_id);
    redis_->hset(key, "total_size",  std::to_string(session.total_size));
    redis_->hset(key, "chunk_size",  std::to_string(session.chunk_size));
    redis_->hset(key, "chunk_count", std::to_string(session.chunk_count));
    redis_->hset(key, "part_hashes", part_hashes_arr.dump());
    redis_->hset(key, "uploaded",    uploaded_arr.dump());
    redis_->expire(key, 24 * 3600);
}

// 从 Redis 反序列化会话；key 不存在或 Redis 未配置时返回空会话
MultipartSession MultipartUploadHandler::load_from_redis(const std::string& upload_id) {
    MultipartSession s;
    if (!redis_) return s;

    std::string key = "multipart:" + upload_id;

    auto file_name = redis_->hget(key, "file_name");
    if (!file_name) return s;

    s.upload_id = upload_id;
    s.file_name = *file_name;

    auto mime = redis_->hget(key, "mime_type");
    s.mime_type = mime.value_or("application/octet-stream");

    auto user = redis_->hget(key, "user_id");
    s.user_id = user.value_or("");

    auto folder = redis_->hget(key, "folder_id");
    s.folder_id = folder.value_or("");

    auto total = redis_->hget(key, "total_size");
    s.total_size = total ? std::stoll(*total) : 0;

    auto chunk_size = redis_->hget(key, "chunk_size");
    s.chunk_size = chunk_size
        ? std::stoll(*chunk_size)
        : static_cast<int64_t>(chunk_size_);

    auto chunk_count = redis_->hget(key, "chunk_count");
    s.chunk_count = chunk_count ? std::stoi(*chunk_count) : 0;

    auto part_hashes_str = redis_->hget(key, "part_hashes");
    if (part_hashes_str && !part_hashes_str->empty()) {
        json arr = json::parse(*part_hashes_str);
        s.part_hashes = arr.get<std::vector<std::string>>();
    }

    auto uploaded_str = redis_->hget(key, "uploaded");
    if (uploaded_str && !uploaded_str->empty()) {
        json arr = json::parse(*uploaded_str);
        auto bool_vec = arr.get<std::vector<bool>>();
        s.uploaded = bool_vec;
    }

    return s;
}

void MultipartUploadHandler::delete_from_redis(const std::string& upload_id) {
    if (!redis_) return;
    redis_->del("multipart:" + upload_id);
}

} // namespace solar_api
