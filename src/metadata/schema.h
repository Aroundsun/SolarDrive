#pragma once

#include "db_pool.h"

namespace solar_metadata {

// 启动时确保数据库 Schema 为当前版本（v2：content_objects + files 分离）
// 返回值：true 表示执行了 DROP 重建（文件/文件夹/分享数据已清空）
bool bootstrap_schema(DbPool& pool);

} // namespace solar_metadata
