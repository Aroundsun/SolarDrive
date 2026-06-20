# SolarDrive API 文档

> 版本：`v1`  
> 基础路径：`http://<host>:8080/api/v1`  
> 默认端口：`8080`

---

## 目录

- [通用说明](#通用说明)
- [端点总览](#端点总览)
- [认证](#认证)
- [文件管理](#文件管理)
- [断点续传](#断点续传)
- [文件分享](#文件分享)
- [系统与监控](#系统与监控)
- [静态资源](#静态资源)
- [错误码](#错误码)

---

## 通用说明

### 请求格式

- 请求体为 JSON 时，需设置：`Content-Type: application/json`
- 上传文件时使用原始二进制 body，文件名通过 Header 传递

### 鉴权

除[白名单接口](#端点总览)外，所有 API 需在 Header 中携带 JWT：

```http
Authorization: Bearer <token>
```

Token 通过 `/api/v1/auth/login` 或 `/api/v1/auth/register` 获取，默认有效期 **7 天**。

### 错误响应

所有错误均返回 JSON：

```json
{
  "error": "错误描述信息"
}
```

HTTP 状态码与 `error` 字段同时表示错误类型（见[错误码](#错误码)）。

---

## 端点总览

| 方法 | 路径 | 鉴权 | 说明 |
|------|------|:----:|------|
| `GET` | `/api/v1/health` | 否 | 健康检查 |
| `GET` | `/metrics` | 否 | Prometheus 指标 |
| `POST` | `/api/v1/auth/register` | 否 | 用户注册 |
| `POST` | `/api/v1/auth/login` | 否 | 用户登录 |
| `POST` | `/api/v1/upload` | 是 | 整文件上传（支持秒传） |
| `GET` | `/api/v1/files` | 是 | 文件列表 |
| `GET` | `/api/v1/files/{id}` | 是 | 下载文件 |
| `DELETE` | `/api/v1/files/{id}` | 是 | 删除文件（软删除） |
| `POST` | `/api/v1/upload/init` | 是 | 初始化断点续传 |
| `PUT` | `/api/v1/upload/{upload_id}/part/{part_num}` | 是 | 上传分片 |
| `GET` | `/api/v1/upload/{upload_id}` | 是 | 查询上传进度 |
| `POST` | `/api/v1/upload/{upload_id}/complete` | 是 | 合并分片，完成上传 |
| `DELETE` | `/api/v1/upload/{upload_id}` | 是 | 取消上传 |
| `POST` | `/api/v1/share` | 是 | 创建分享链接 |
| `GET` | `/api/v1/shares` | 是 | 我的分享列表 |
| `DELETE` | `/api/v1/share/{token}` | 是 | 撤销分享 |
| `GET` | `/s/{token}` | 否 | 通过分享链接下载 |

---

## 认证

### POST `/api/v1/auth/register`

注册新用户，成功后直接返回 JWT（自动登录）。

**请求体：**

| 字段 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `username` | string | 是 | 用户名，不可重复 |
| `password` | string | 是 | 密码 |

**成功响应 `200`：**

```json
{
  "user_id": "5764cccc-7aa6-449b-80e5-3196fbf57bd0",
  "username": "alice",
  "token": "eyJhbGciOiJIUzI1NiIs..."
}
```

**错误：**

| 状态码 | 说明 |
|--------|------|
| `400` | 用户名或密码为空 / JSON 格式错误 |
| `500` | 用户名已存在或数据库错误 |

**示例：**

```bash
curl -X POST http://localhost:8080/api/v1/auth/register \
  -H "Content-Type: application/json" \
  -d '{"username":"alice","password":"pass123"}'
```

---

### POST `/api/v1/auth/login`

用户登录，返回 JWT。

**请求体：**

| 字段 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `username` | string | 是 | 用户名 |
| `password` | string | 是 | 密码 |

**成功响应 `200`：**

```json
{
  "user_id": "5764cccc-7aa6-449b-80e5-3196fbf57bd0",
  "username": "alice",
  "token": "eyJhbGciOiJIUzI1NiIs..."
}
```

**错误：**

| 状态码 | 说明 |
|--------|------|
| `400` | 用户名或密码为空 |
| `401` | 用户名或密码错误 |

**示例：**

```bash
curl -X POST http://localhost:8080/api/v1/auth/login \
  -H "Content-Type: application/json" \
  -d '{"username":"alice","password":"pass123"}'
```

---

## 文件管理

> 以下接口均需在 Header 中携带 `Authorization: Bearer <token>`

### POST `/api/v1/upload`

整文件上传，请求体为文件原始二进制内容。相同内容（SHA-256 相同）自动秒传。

**请求 Header：**

| Header | 必填 | 说明 |
|--------|:----:|------|
| `Authorization` | 是 | `Bearer <token>` |
| `X-File-Name` | 否 | 文件名，支持 URL 编码（中文文件名建议 encodeURIComponent） |
| `Content-Type` | 否 | MIME 类型，默认 `application/octet-stream` |

**请求体：** 文件二进制内容

**成功响应 `200`（正常上传）：**

```json
{
  "file_id": "a1b2c3d4-....",
  "hash": "b94d27b9934d3e08...",
  "size": 12345,
  "chunks": 3,
  "instant": false
}
```

**成功响应 `200`（秒传）：**

```json
{
  "file_id": "a1b2c3d4-....",
  "hash": "b94d27b9934d3e08...",
  "size": 12345,
  "instant": true
}
```

**错误：**

| 状态码 | 说明 |
|--------|------|
| `400` | 请求体为空 |
| `401` | 未登录或 Token 无效 |
| `500` | 存储或数据库错误 |

**示例：**

```bash
curl -X POST http://localhost:8080/api/v1/upload \
  -H "Authorization: Bearer $TOKEN" \
  -H "X-File-Name: report.pdf" \
  -H "Content-Type: application/pdf" \
  --data-binary @report.pdf
```

---

### GET `/api/v1/files`

获取当前所有未删除文件的列表。

**成功响应 `200`：**

```json
{
  "files": [
    {
      "id": "a1b2c3d4-....",
      "name": "report.pdf",
      "size": 12345,
      "hash": "b94d27b9934d3e08...",
      "mime_type": "application/pdf",
      "created_at": "2026-06-20T10:30:00Z"
    }
  ]
}
```

**示例：**

```bash
curl http://localhost:8080/api/v1/files \
  -H "Authorization: Bearer $TOKEN"
```

---

### GET `/api/v1/files/{id}`

下载指定文件。

**路径参数：**

| 参数 | 说明 |
|------|------|
| `id` | 文件 UUID |

**成功响应 `200`：**

- Content-Type：文件 MIME 类型
- Content-Disposition：`attachment; filename="..."`
- Body：文件二进制内容

**错误：**

| 状态码 | 说明 |
|--------|------|
| `400` | 缺少文件 ID |
| `401` | 未登录 |
| `404` | 文件不存在或已删除 |

**示例：**

```bash
curl http://localhost:8080/api/v1/files/<file_id> \
  -H "Authorization: Bearer $TOKEN" \
  -o report.pdf
```

---

### DELETE `/api/v1/files/{id}`

软删除文件（保留元数据，标记 `deleted_at`）。

**路径参数：**

| 参数 | 说明 |
|------|------|
| `id` | 文件 UUID |

**成功响应 `200`：**

```json
{
  "deleted": true
}
```

**示例：**

```bash
curl -X DELETE http://localhost:8080/api/v1/files/<file_id> \
  -H "Authorization: Bearer $TOKEN"
```

---

## 断点续传

适用于大文件，默认分片大小 **4 MB**（`4194304` 字节）。上传会话存储在 Redis，重启 Redis 后会话丢失。

### POST `/api/v1/upload/init`

初始化分片上传任务。

**请求体：**

| 字段 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `file_name` | string | 是 | 文件名 |
| `total_size` | int64 | 是 | 文件总大小（字节） |
| `mime_type` | string | 否 | MIME 类型，默认 `application/octet-stream` |

**成功响应 `200`：**

```json
{
  "upload_id": "a1b2c3d4-e5f6-....",
  "chunk_size": 4194304,
  "chunk_count": 256
}
```

**示例：**

```bash
curl -X POST http://localhost:8080/api/v1/upload/init \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"file_name":"video.mp4","total_size":1073741824,"mime_type":"video/mp4"}'
```

---

### PUT `/api/v1/upload/{upload_id}/part/{part_num}`

上传单个分片。

**路径参数：**

| 参数 | 说明 |
|------|------|
| `upload_id` | 初始化返回的上传 ID |
| `part_num` | 分片序号，从 `0` 开始 |

**请求体：** 分片二进制内容（≤ 4 MB）

**成功响应 `200`：**

```json
{
  "part_num": 0,
  "hash": "b94d27b9934d3e08...",
  "size": 4194304
}
```

**错误：**

| 状态码 | 说明 |
|--------|------|
| `400` | 分片为空 / part_num 无效 |
| `404` | 上传会话不存在 |

**示例：**

```bash
curl -X PUT "http://localhost:8080/api/v1/upload/<upload_id>/part/0" \
  -H "Authorization: Bearer $TOKEN" \
  --data-binary @part0.bin
```

---

### GET `/api/v1/upload/{upload_id}`

查询上传进度，用于断点恢复。

**成功响应 `200`：**

```json
{
  "upload_id": "a1b2c3d4-....",
  "file_name": "video.mp4",
  "total_size": 1073741824,
  "chunk_size": 4194304,
  "chunk_count": 256,
  "uploaded_count": 128,
  "part_hashes": ["hash0", "hash1", null, "..."]
}
```

- `part_hashes` 中 `null` 表示该分片尚未上传

**示例：**

```bash
curl "http://localhost:8080/api/v1/upload/<upload_id>" \
  -H "Authorization: Bearer $TOKEN"
```

---

### POST `/api/v1/upload/{upload_id}/complete`

所有分片上传完成后调用，合并写入数据库。

**成功响应 `200`：**

```json
{
  "file_id": "a1b2c3d4-....",
  "hash": "b94d27b9934d3e08...",
  "size": 1073741824
}
```

**错误：**

| 状态码 | 说明 |
|--------|------|
| `400` | 仍有分片未上传 |
| `404` | 上传会话不存在 |

**示例：**

```bash
curl -X POST "http://localhost:8080/api/v1/upload/<upload_id>/complete" \
  -H "Authorization: Bearer $TOKEN"
```

---

### DELETE `/api/v1/upload/{upload_id}`

取消上传，清除 Redis 中的上传会话。

**成功响应 `200`：**

```json
{
  "status": "aborted"
}
```

**示例：**

```bash
curl -X DELETE "http://localhost:8080/api/v1/upload/<upload_id>" \
  -H "Authorization: Bearer $TOKEN"
```

---

## 文件分享

### POST `/api/v1/share`

为指定文件创建分享链接（需鉴权）。

**请求体：**

| 字段 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `file_id` | string | 是 | 文件 UUID |
| `password` | string | 否 | 访问密码，留空表示公开 |
| `expires_in_hours` | int | 否 | 有效时长（小时），`0` 或不传表示永不过期 |
| `max_downloads` | int | 否 | 最大下载次数，`0` 表示不限制 |

**成功响应 `200`：**

```json
{
  "share_token": "a1b2c3d4",
  "url": "/s/a1b2c3d4",
  "expires_at": "2026-06-21T15:00:00+08:00",
  "has_password": true
}
```

**示例：**

```bash
curl -X POST http://localhost:8080/api/v1/share \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"file_id":"<file_id>","password":"secret","expires_in_hours":24}'
```

---

### GET `/api/v1/shares`

查询当前用户的分享列表（需鉴权）。

**成功响应 `200`：**

```json
[
  {
    "share_token": "a1b2c3d4",
    "file_id": "uuid",
    "url": "/s/a1b2c3d4",
    "download_count": 3,
    "max_downloads": 0,
    "has_password": true,
    "created_at": "2026-06-20 10:00:00+08",
    "expires_at": null
  }
]
```

---

### DELETE `/api/v1/share/{token}`

撤销分享（需鉴权，仅分享创建者可操作）。

**成功响应 `200`：**

```json
{"revoked": true}
```

---

### GET `/s/{token}`

通过分享链接下载文件，**无需 JWT 鉴权**。

- 若分享设置了密码，通过 query 参数或 Header 传递：
  - `GET /s/{token}?password=secret`
  - 或 `X-Share-Password: secret`

**成功响应 `200`：** 文件二进制流，`Content-Disposition: attachment`

**错误：**

| 状态码 | 说明 |
|--------|------|
| `401` | 需要密码 |
| `403` | 密码错误或下载次数已达上限 / 分享已撤销 |
| `404` | 分享或原文件不存在 |
| `410` | 分享已过期 |

**示例：**

```bash
curl -OJ "http://localhost:8080/s/a1b2c3d4?password=secret"
```

---

## 系统与监控

### GET `/api/v1/health`

健康检查，无需鉴权。

**成功响应 `200`：**

```json
{
  "status": "ok",
  "service": "SolarDrive"
}
```

---

### GET `/metrics`

Prometheus 文本格式指标，无需鉴权。可直接被 Prometheus / Grafana 抓取。

**成功响应 `200`：**

```
Content-Type: text/plain; version=0.0.4; charset=utf-8

# HELP solardrive_requests_total Total HTTP requests processed
# TYPE solardrive_requests_total counter
solardrive_requests_total 42

# HELP solardrive_errors_total Total HTTP errors returned
# TYPE solardrive_errors_total counter
solardrive_errors_total 1

# HELP solardrive_upload_bytes_total Total bytes uploaded
# TYPE solardrive_upload_bytes_total counter
solardrive_upload_bytes_total 1048576

# HELP solardrive_download_bytes_total Total bytes downloaded
# TYPE solardrive_download_bytes_total counter
solardrive_download_bytes_total 524288

# HELP solardrive_active_connections Current active TCP connections
# TYPE solardrive_active_connections gauge
solardrive_active_connections 3
```

**可视化面板：** 浏览器访问 `/metrics.html`

---

## 静态资源

以下路径无需鉴权，由静态文件服务直接返回：

| 路径 | 说明 |
|------|------|
| `/` | Web 云盘主页 |
| `/index.html` | 同上 |
| `/app.js` | 云盘前端脚本 |
| `/style.css` | 样式表 |
| `/metrics.html` | 监控面板 |
| `/metrics.js` | 监控面板脚本 |

---

## 错误码

| HTTP 状态码 | 含义 | 常见原因 |
|-------------|------|----------|
| `200` | 成功 | — |
| `400` | 请求参数错误 | 缺少必填字段、body 为空 |
| `401` | 未授权 | 缺少 Token、Token 过期或无效 |
| `404` | 资源不存在 | 文件/上传会话未找到 |
| `500` | 服务器内部错误 | 数据库、存储、Redis 异常 |

---

## 完整调用示例

```bash
BASE=http://localhost:8080

# 1. 注册
TOKEN=$(curl -s -X POST $BASE/api/v1/auth/register \
  -H "Content-Type: application/json" \
  -d '{"username":"demo","password":"demo1234"}' \
  | python3 -c "import sys,json; print(json.load(sys.stdin)['token'])")

# 2. 上传
curl -X POST $BASE/api/v1/upload \
  -H "Authorization: Bearer $TOKEN" \
  -H "X-File-Name: hello.txt" \
  -H "Content-Type: text/plain" \
  --data-binary "Hello SolarDrive"

# 3. 列表
curl -s $BASE/api/v1/files -H "Authorization: Bearer $TOKEN"

# 4. 健康检查
curl -s $BASE/api/v1/health

# 5. 监控指标
curl -s $BASE/metrics
```
