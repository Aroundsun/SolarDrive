// =============================================================================
// object_store.cpp — 内容寻址对象存储实现
// SolarDrive 存储层：OpenSSL SHA-256、两级目录散列、mkdir 按需创建
// =============================================================================
#include "object_store.h"

#include <openssl/evp.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <stdexcept>

namespace solar_storage {

ObjectStore::ObjectStore(const std::string& base_path, size_t chunk_size)
    : base_path_(base_path)
    , chunk_size_(chunk_size > 0 ? chunk_size : DEFAULT_CHUNK_SIZE)
{
    // 确保 base_path_ 不以 '/' 结尾（统一处理）
    if (!base_path_.empty() && base_path_.back() == '/') {
        base_path_.pop_back();
    }

    // 创建 base 目录（递归）
    // mkdir -p 效果
    std::string path = base_path_;
    // 逐级创建
    size_t pos = 0;
    while ((pos = path.find('/', pos + 1)) != std::string::npos) {
        std::string sub = path.substr(0, pos);
        ::mkdir(sub.c_str(), 0755);
    }
    ::mkdir(path.c_str(), 0755);
}

std::string ObjectStore::sha256(const std::string& data)
{
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        throw std::runtime_error("EVP_MD_CTX_new failed");
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP_DigestInit_ex failed");
    }

    if (EVP_DigestUpdate(ctx, data.data(), data.size()) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP_DigestUpdate failed");
    }

    if (EVP_DigestFinal_ex(ctx, hash, &hash_len) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP_DigestFinal_ex failed");
    }

    EVP_MD_CTX_free(ctx);

    // 转换为 hex string
    std::ostringstream oss;
    for (unsigned int i = 0; i < hash_len; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return oss.str();
}

std::string ObjectStore::hash_to_path(const std::string& hash) const
{
    // /base/ab/cd/<fullhash>
    if (hash.size() < 4) {
        throw std::runtime_error("hash too short: " + hash);
    }
    return base_path_ + "/" +
           hash.substr(0, 2) + "/" +
           hash.substr(2, 2) + "/" +
           hash;
}

std::string ObjectStore::put(const std::string& data)
{
    std::string hash = sha256(data);

    // 去重：如果文件已存在则不重复写入
    if (exists(hash)) {
        return hash;
    }

    std::string path = hash_to_path(hash);

    // 确保父目录存在
    // path = /base/ab/cd/<hash>
    // 需要创建 /base/ab 和 /base/ab/cd
    size_t last_slash = path.rfind('/');
    if (last_slash != std::string::npos) {
        std::string dir = path.substr(0, last_slash);
        // 再找上一级目录
        size_t prev_slash = dir.rfind('/');
        if (prev_slash != std::string::npos) {
            std::string parent_dir = dir.substr(0, prev_slash);
            ::mkdir(parent_dir.c_str(), 0755);
        }
        ::mkdir(dir.c_str(), 0755);
    }

    // 写入文件
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        throw std::runtime_error("cannot open file for writing: " + path);
    }
    ofs.write(data.data(), data.size());
    ofs.close();

    return hash;
}

std::string ObjectStore::get(const std::string& hash) const
{
    std::string path = hash_to_path(hash);

    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        throw std::runtime_error("object not found: " + hash);
    }

    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

bool ObjectStore::exists(const std::string& hash) const
{
    std::string path = hash_to_path(hash);
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
}

std::vector<std::string> ObjectStore::put_chunked(const std::string& data)
{
    std::vector<std::string> hashes;

    // 按 CHUNK_SIZE 切分，每块独立 put（可跨文件复用相同 chunk）
    size_t total = data.size();
    size_t offset = 0;

    while (offset < total) {
        size_t chunk_len = std::min(total - offset, chunk_size_);
        std::string chunk = data.substr(offset, chunk_len);
        std::string hash = put(chunk);
        hashes.push_back(hash);
        offset += chunk_len;
    }

    // 空数据也返回一个 chunk
    if (hashes.empty()) {
        hashes.push_back(put(""));
    }

    return hashes;
}

std::string ObjectStore::get_chunked(const std::vector<std::string>& hashes) const
{
    // 按元数据记录的 hash 顺序拼接各 chunk
    std::string result;
    for (const auto& hash : hashes) {
        result += get(hash);
    }
    return result;
}

} // namespace solar_storage
