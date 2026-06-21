# 文件删除 / Content GC 缺陷修复记录（冒烟测试发现）

**日期：** 2026-06-21  
**发现方式：** 新增 `scripts/smoke_test.sh` 端到端冒烟时，第 11 步 `DELETE /api/v1/files/{id}` 长时间无响应或返回 500  
**涉及文件：** `src/metadata/content_gc.cpp`  
**关联脚本：** `scripts/smoke_test.sh`、`scripts/demo.sh`

---

## 背景

删除文件的业务路径为（`FileHandler::handle_delete`）：

1. `file_dao_.soft_delete(file_id)` — 将 `files.deleted_at` 置为当前时间（软删除）
2. `share_dao_.revoke_by_file_id(file_id)` — 撤销关联分享
3. `content_gc_.try_collect_content(content_id)` — 若无活跃引用，回收 `content_objects` 行及磁盘 chunk

`ContentGc::purge_content()` 在删除 content 前会调用 `active_refcount()`，仅统计 **`deleted_at IS NULL`** 的 `files` 行。软删除后计数为 0，逻辑上认为可以回收 content。

但 schema 中 `files.content_id` 对 `content_objects(id)` 有 **NOT NULL 外键**，且 **不区分是否软删除**：

```sql
content_id UUID NOT NULL REFERENCES content_objects(id)
```

因此 tombstone（`deleted_at IS NOT NULL`）的 `files` 行仍持有外键，`DELETE FROM content_objects` 会被 PostgreSQL 拒绝。

---

## Bug：软删除 tombstone 阻塞 Content GC

### 现象

冒烟测试完整流程（含创建分享、公开下载、撤销分享后再删文件）时：

- 第 11 步 `DELETE /api/v1/files/{id}` **挂起 90s～130s**，客户端长时间收不到完整响应
- 最终可能返回 **HTTP 500**，响应体类似：

```json
{
  "error": "delete failed: ERROR:  update or delete on table \"content_objects\" violates foreign key constraint \"files_content_id_fkey\" on table \"files\"\nDETAIL:  Key (id)=(...) is still referenced from table \"files\".\n"
}
```

仅 upload + delete（不经过分享）时，错误更快出现（约十几秒），但同样为 500。

`scripts/smoke_test.sh` 未设置 curl 超时时，表现为第 11 步**一直卡住**，误以为服务死锁。

### 根因

| 步骤 | 实际数据库状态 | GC 假设 |
|------|----------------|---------|
| `soft_delete` | `files` 行仍在，`deleted_at` 已设置，`content_id` 外键仍有效 | — |
| `active_refcount == 0` | tombstone 行不参与计数 | 认为无引用 |
| `delete_by_id_in_txn` | 外键仍指向 content | 尝试 `DELETE content_objects` → **FK 违反** |

此外，`file_shares.file_id` 也引用 `files(id)`。若直接硬删 tombstone 而不清理分享行，会触发第二层外键错误。当前 `revoke_by_file_id` 仅 `UPDATE is_revoked`，**分享记录仍在**。

### 影响

- 用户删除文件后接口失败或极慢，磁盘 chunk 无法回收（存储泄漏）
- 冒烟 / Demo 无法走通 delete 步骤
- 定时任务 `content_gc.collect_orphans()` 对同类 orphan 同样失败

---

## 修复

在 `ContentGc::purge_content()` 中，**于同一事务内**、在删除 `content_objects` 之前：

1. 删除指向 tombstone 文件的 `file_shares` 行  
2. 硬删除 `deleted_at IS NOT NULL` 的 tombstone `files` 行  
3. 再执行原有的 `delete_by_id_in_txn` 与 chunk 磁盘清理

```cpp
// 软删除的 files 行仍持有 content_id 外键，需先清理 tombstone 及关联分享
txn.exec_params(
    "DELETE FROM file_shares "
    "WHERE file_id IN ("
    "  SELECT id FROM files WHERE content_id = $1 AND deleted_at IS NOT NULL"
    ")",
    content.id
);
txn.exec_params(
    "DELETE FROM files WHERE content_id = $1 AND deleted_at IS NOT NULL",
    content.id
);
if (!content_dao_.delete_by_id_in_txn(txn, content.id)) {
    return stats;
}
```

设计说明：

- **仍保留软删除语义**：删除 API 第一步仍是 `soft_delete`，便于将来扩展「回收站 / 恢复」；即时 GC 只在确认无活跃引用后物理清理 tombstone。
- **分享先删后删文件**：满足 `file_shares → files` 外键顺序。
- **与 `delete_by_id_in_txn` 条件一致**：后者仍要求「无 `deleted_at IS NULL` 的引用」；清 tombstone 后条件成立。

---

## 验证方式

### API 冒烟（推荐）

```bash
# 本地二进制（示例：8081 端口）
docker compose up -d          # 或确保 Postgres / Redis 可用
cmake --build build -j$(nproc)
# 启动 solardrive 后：
BASE_URL=http://127.0.0.1:8080 ./scripts/smoke_test.sh

# 一键 Demo
./scripts/demo.sh
```

修复后预期：**11 步全部通过**，总耗时约数秒～十秒级；最后一步输出 `file deleted` 与 `ALL SMOKE TESTS PASSED`。

### 手工最小复现

```bash
# register → login → upload → share → revoke share → delete file
# delete 应快速返回 {"deleted":true}，HTTP 200
```

---

## 关联交付（同次任务）

| 路径 | 说明 |
|------|------|
| `scripts/smoke_test.sh` | API 冒烟：health → register → login → upload → list → download → share → 公开下载 → revoke → delete |
| `scripts/demo.sh` | `docker compose up -d --build` + 等待 health + 调用冒烟脚本 |
| `scripts/smoke_test.sh` | 为 `curl` 增加 `--max-time`（默认 30s），避免 FK 错误时无限等待 |

---

## 经验总结

1. **软删除 + 外键**：`active_refcount` 与 FK 约束是两个不同层面；「逻辑上无活跃引用」≠「数据库允许删 parent 行」。
2. **删除顺序**：有多层 FK 时（`file_shares → files → content_objects`），GC 必须按依赖顺序物理清理。
3. **端到端冒烟能暴露集成问题**：单元测试覆盖 JWT / 存储等模块，但「删文件 + 分享 + GC」的跨表路径需要 API 级脚本验证。
4. **curl 应设超时**：集成脚本中对每个请求设置 `--max-time`，避免掩盖「慢失败」为「假死锁」。

---

## 相关提交范围

- 修复：`src/metadata/content_gc.cpp`
- 脚本：`scripts/smoke_test.sh`、`scripts/demo.sh`
- 文档：本文档、`README.md`（Demo / 冒烟用法）
