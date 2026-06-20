#include "multipart_upload_handler.h"
#include "../auth/jwt.h"
#include <nlohmann/json.hpp>
#include <ctime>
#include <sstream>
#include <random>

using json = nlohmann::json;

namespace solar_api {

// 生成简单的 UUID（用于 upload_id）
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
    std::shared_ptr<solar_cache::RedisClient> redis)
    : store_(store), dao_(dao), redis_(std::move(redis)) {}

// -------- Init --------

void MultipartUploadHandler::handle_init(
    const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
    try {
        json body = json::parse(req.body());
        std::string file_name = body["file_name"].get<std::string>();
        int64_t total_size    = body["total_size"].get<int64_t>();
        std::string mime_type = body.value("mime_type", "application/octet-stream");

        if (file_name.empty() || total_size <= 0) {
            resp.set_error(400, "file_name and total_size are required");
            return;
        }

        MultipartSession session;
        session.upload_id    = generate_uuid();
        session.user_id      = "";
        session.file_name    = file_name;
        session.mime_type    = mime_type;
        session.total_size   = total_size;
        session.chunk_size   = 4 * 1024 * 1024;  // 4MB
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

// -------- Upload Part --------

void MultipartUploadHandler::handle_upload_part(
    const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
    try {
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
        if (part_num < 0 || part_num >= session.chunk_count) {
            resp.set_error(400, "invalid part_num");
            return;
        }

        // 存储分片数据到对象存储
        std::string hash = store_.put(req.body());
        session.uploaded[part_num] = true;
        session.part_hashes[part_num] = hash;
        save_to_redis(session);

        json j;
        j["part_num"] = part_num;
        j["hash"]     = hash;
        j["size"]     = req.body().size();
        resp.set_json(j.dump());
    } catch (const std::exception& e) {
        resp.set_error(500, std::string("upload part failed: ") + e.what());
    }
}

// -------- Complete --------

void MultipartUploadHandler::handle_complete(
    const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
    try {
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

        // 写入数据库
        std::string file_id = dao_.insert(f);

        // 写入 Redis 秒传索引
        if (redis_) {
            redis_->set("file:hash:" + f.hash, file_id);
        }

        // 删除 Redis 会话
        delete_from_redis(upload_id);

        json j;
        j["file_id"] = file_id;
        j["hash"]    = f.hash;
        j["size"]    = f.size;
        resp.set_json(j.dump());
    } catch (const std::exception& e) {
        resp.set_error(500, std::string("complete failed: ") + e.what());
    }
}

// -------- Status --------

void MultipartUploadHandler::handle_status(
    const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
    try {
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

// -------- Abort --------

void MultipartUploadHandler::handle_abort(
    const solar_http::HttpRequest& req, solar_http::HttpResponse& resp) {
    try {
        std::string upload_id = req.get_path_param("upload_id");
        if (upload_id.empty()) {
            resp.set_error(400, "missing upload_id");
            return;
        }

        delete_from_redis(upload_id);
        resp.set_json(R"({"status":"aborted"})");
    } catch (const std::exception& e) {
        resp.set_error(500, std::string("abort failed: ") + e.what());
    }
}

// -------- Upload with Redis dedup (replaces old upload handler) --------

void MultipartUploadHandler::handle_upload_with_redis(
    const solar_http::HttpRequest& req,
    solar_http::HttpResponse& resp,
    solar_storage::ObjectStore& store,
    solar_metadata::FileDao& dao,
    std::shared_ptr<solar_cache::RedisClient> redis) {

    if (req.body().empty()) {
        resp.set_error(400, "Empty body");
        return;
    }

    std::string file_hash = solar_storage::ObjectStore::sha256(req.body());

    // ① 优先查 Redis
    if (redis) {
        auto cached_id = redis->get("file:hash:" + file_hash);
        if (cached_id) {
            json j;
            j["file_id"] = *cached_id;
            j["hash"]    = file_hash;
            j["size"]    = req.body().size();
            j["instant"] = true;
            resp.set_json(j.dump());
            return;
        }
    }

    // ② 查 DB（兜底）
    auto existing = dao.find_by_hash(file_hash);
    if (existing) {
        if (redis) {
            redis->set("file:hash:" + file_hash, existing->id);
        }
        json j;
        j["file_id"] = existing->id;
        j["hash"]    = file_hash;
        j["size"]    = existing->size;
        j["instant"] = true;
        resp.set_json(j.dump());
        return;
    }

    // ③ 写入对象存储
    auto chunks = store.put_chunked(req.body());

    // ④ 元数据
    solar_metadata::FileRecord f;
    f.name      = req.get_header("X-File-Name");
    if (f.name.empty()) f.name = "unnamed";
    f.size      = req.body().size();
    f.hash      = file_hash;
    f.mime_type = req.get_header("Content-Type");
    if (f.mime_type.empty()) f.mime_type = "application/octet-stream";

    json chunk_arr = json::array();
    for (auto& c : chunks) chunk_arr.push_back(c);
    f.chunk_hashes = chunk_arr.dump();

    std::string file_id = dao.insert(f);

    // ⑤ 写入 Redis 缓存
    if (redis) {
        redis->set("file:hash:" + file_hash, file_id);
    }

    json j;
    j["file_id"] = file_id;
    j["hash"]    = file_hash;
    j["size"]    = f.size;
    j["chunks"]  = chunks.size();
    j["instant"] = false;
    resp.set_json(j.dump());
}

// -------- Redis 会话操作 --------

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
    redis_->hset(key, "total_size",  std::to_string(session.total_size));
    redis_->hset(key, "chunk_size",  std::to_string(session.chunk_size));
    redis_->hset(key, "chunk_count", std::to_string(session.chunk_count));
    redis_->hset(key, "part_hashes", part_hashes_arr.dump());
    redis_->hset(key, "uploaded",    uploaded_arr.dump());
}

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

    auto total = redis_->hget(key, "total_size");
    s.total_size = total ? std::stoll(*total) : 0;

    auto chunk_size = redis_->hget(key, "chunk_size");
    s.chunk_size = chunk_size ? std::stoll(*chunk_size) : 4 * 1024 * 1024;

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
