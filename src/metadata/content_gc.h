#pragma once

#include "content_dao.h"
#include "../storage/object_store.h"

namespace solar_metadata {

struct GcStats {
    int content_rows_deleted = 0;
    int chunk_files_removed  = 0;
};

/// 孤儿 content_objects 与对象存储块回收
class ContentGc {
public:
    ContentGc(ContentDao& content_dao, DbPool& pool, solar_storage::ObjectStore& store);

    /// 批量扫描并删除无引用的 content 行及可安全移除的 chunk 文件
    GcStats collect_orphans(int batch_limit = 100);

    /// 文件软删除后立即尝试回收对应 content（无引用时）
    GcStats try_collect_content(const std::string& content_id);

private:
    GcStats purge_content(const ContentRecord& content);

    ContentDao&                 content_dao_;
    DbPool&                     pool_;
    solar_storage::ObjectStore& store_;
};

} // namespace solar_metadata
