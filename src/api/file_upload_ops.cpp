#include "file_upload_ops.h"
#include "../auth/access_control.h"
#include "upload_limits.h"

#include <nlohmann/json.hpp>
#include <stdexcept>

using json = nlohmann::json;

namespace solar_api {

namespace {

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string url_decode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            const int hi = hex_value(s[i + 1]);
            const int lo = hex_value(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        if (s[i] == '+') {
            out += ' ';
            continue;
        }
        out += s[i];
    }
    return out;
}

std::string resolve_filename(const solar_http::HttpRequest& req) {
    std::string filename = req.get_header("X-File-Name");
    if (!filename.empty()) {
        filename = url_decode(filename);
    }
    return filename.empty() ? "unnamed" : filename;
}

std::string resolve_mime_type(const solar_http::HttpRequest& req) {
    std::string mime = req.get_header("Content-Type");
    return mime.empty() ? "application/octet-stream" : mime;
}

} // namespace

std::string link_existing_content(
    solar_metadata::FileDao& file_dao,
    const solar_metadata::FileRecord& content,
    const std::string& filename,
    const std::string& mime_type,
    const std::string& user_id,
    const std::string& folder_id) {
    solar_metadata::FileRecord entry;
    entry.name         = file_dao.make_unique_name(filename, user_id, folder_id);
    entry.size         = content.size;
    entry.hash         = content.hash;
    entry.chunk_hashes = content.chunk_hashes;
    entry.mime_type    = mime_type.empty() ? content.mime_type : mime_type;
    entry.owner_id     = user_id;
    entry.folder_id    = folder_id;
    return file_dao.insert_link(entry, content.content_id);
}

void handle_single_upload(
    const solar_http::HttpRequest& req,
    solar_http::HttpResponse& resp,
    const std::string& user_id,
    solar_storage::ObjectStore& store,
    solar_metadata::FileDao& file_dao,
    solar_metadata::FolderDao& folder_dao,
    const std::shared_ptr<solar_cache::RedisClient>& redis,
    int64_t max_file_size_bytes) {
    if (req.body().empty()) {
        resp.set_error(400, "Empty body");
        return;
    }
    if (reject_oversized(static_cast<int64_t>(req.body().size()), max_file_size_bytes, resp)) {
        return;
    }

    const std::string folder_header = req.get_header("X-Folder-Id");
    auto folder_id = solar_auth::resolve_folder_id(
        folder_header, user_id, folder_dao, resp);
    if (!folder_id) {
        return;
    }

    const std::string filename = resolve_filename(req);
    const std::string mime_type = resolve_mime_type(req);
    const std::string file_hash = solar_storage::ObjectStore::sha256(req.body());

    if (redis) {
        if (redis->get("file:hash:" + file_hash)) {
            if (auto content = file_dao.find_content_by_hash(file_hash)) {
                const std::string file_id = link_existing_content(
                    file_dao, *content, filename, mime_type, user_id, *folder_id);
                json j;
                j["file_id"] = file_id;
                j["hash"]    = file_hash;
                j["size"]    = content->size;
                j["instant"] = true;
                resp.set_json(j.dump());
                return;
            }
        }
    }

    if (auto content = file_dao.find_content_by_hash(file_hash)) {
        if (redis) {
            redis->set("file:hash:" + file_hash, content->content_id);
        }
        const std::string file_id = link_existing_content(
            file_dao, *content, filename, mime_type, user_id, *folder_id);
        json j;
        j["file_id"] = file_id;
        j["hash"]    = file_hash;
        j["size"]    = content->size;
        j["instant"] = true;
        resp.set_json(j.dump());
        return;
    }

    auto chunks = store.put_chunked(req.body());
    json chunk_arr = json::array();
    for (const auto& c : chunks) {
        chunk_arr.push_back(c);
    }

    solar_metadata::FileRecord entry;
    entry.name         = filename;
    entry.size         = static_cast<int64_t>(req.body().size());
    entry.hash         = file_hash;
    entry.chunk_hashes = chunk_arr.dump();
    entry.mime_type    = mime_type;
    entry.owner_id     = user_id;
    entry.folder_id    = *folder_id;

    const std::string file_id = file_dao.insert(entry);
    if (redis) {
        if (auto content = file_dao.find_content_by_hash(file_hash)) {
            redis->set("file:hash:" + file_hash, content->content_id);
        }
    }

    json j;
    j["file_id"] = file_id;
    j["hash"]    = file_hash;
    j["size"]    = entry.size;
    j["chunks"]  = chunks.size();
    j["instant"] = false;
    resp.set_json(j.dump());
}

} // namespace solar_api
