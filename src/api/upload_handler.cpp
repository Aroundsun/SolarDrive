#include "upload_handler.h"

#include <nlohmann/json.hpp>
#include <pqxx/except>

#include <stdexcept>

using json = nlohmann::json;

namespace solar_api {

void UploadHandler::handle(const solar_http::HttpRequest& req,
                           solar_http::HttpResponse& resp)
{
    try {
        // 1. 检查 body 是否为空
        const std::string& body = req.body();
        if (body.empty()) {
            resp.set_error(400, "request body is empty");
            return;
        }

        // 从 headers 或 query 获取文件名和 MIME 类型
        std::string filename = req.get_header("X-File-Name");
        if (filename.empty()) {
            filename = "unnamed";
        }
        std::string mime_type = req.get_header("Content-Type");
        if (mime_type.empty()) {
            mime_type = "application/octet-stream";
        }

        // 2. 计算全文 SHA-256
        std::string hash = solar_storage::ObjectStore::sha256(body);

        // 3. 通过 dao_.find_by_hash() 查是否已存在（秒传）
        auto existing = dao_.find_by_hash(hash);
        if (existing.has_value()) {
            // 秒传：文件已存在，直接返回
            json resp_json;
            resp_json["file_id"]    = existing->id;
            resp_json["hash"]       = hash;
            resp_json["size"]       = existing->size;
            resp_json["chunks"]     = json::parse(existing->chunk_hashes);
            resp_json["instant"]    = true;
            resp.set_json(resp_json.dump());
            return;
        }

        // 4. 不存在则调用 store_.put_chunked() 分块存储
        std::vector<std::string> chunk_hashes = store_.put_chunked(body);

        // 将 chunk_hashes 转为 JSON 数组字符串
        json chunk_json = json::array();
        for (const auto& h : chunk_hashes) {
            chunk_json.push_back(h);
        }
        std::string chunk_hashes_str = chunk_json.dump();

        // 5. 构建 FileRecord，调用 dao_.insert() 写入元数据
        solar_metadata::FileRecord rec;
        rec.name         = filename;
        rec.size         = static_cast<int64_t>(body.size());
        rec.hash         = hash;
        rec.chunk_hashes = chunk_hashes_str;
        rec.mime_type    = mime_type;

        std::string file_id = dao_.insert(rec);

        // 6. 返回 JSON：{file_id, hash, size, chunks, instant}
        json resp_json;
        resp_json["file_id"] = file_id;
        resp_json["hash"]    = hash;
        resp_json["size"]    = body.size();
        resp_json["chunks"]  = chunk_json;
        resp_json["instant"] = false;
        resp.set_json(resp_json.dump());

    } catch (const pqxx::failure& e) {
        resp.set_error(500, "database error");
    } catch (const std::exception& e) {
        resp.set_error(500, e.what());
    }
}

} // namespace solar_api
