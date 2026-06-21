# SolarDrive — 自研 Reactor 网络模型的 C++17 云存储服务

SolarDrive 是一个从网络层到存储层**全链路自研**的个人云盘后端：基于 **Reactor 多线程模型**（Epoll + EventLoop + 线程池）与 **Content-Addressable 对象存储**（SHA-256 寻址、4MB 分块），提供文件上传、下载、秒传去重与断点续传等完整能力。

在安全与运维方面，内置 **JWT 鉴权**、**Redis 缓存**、**YAML 配置**与 **spdlog 结构化日志**；附带 **Web 控制台**与 **Prometheus 监控面板**，并支持 **Docker Compose 一键部署**（PostgreSQL + Redis + 服务本体）。

> 项目状态：**Phase 3 完成** — 核心链路与生产化基础设施已就绪，可本地开发或容器化部署。

---

## 系统架构

```
                     ┌─────────────────────┐
                     │  浏览器 / curl 客户端 │
                     └────────┬────────────┘
                              │
              ┌───────────────▼───────────────┐
              │     Reactor 多线程网络层       │
              │ (Epoll + EventLoop + 线程池)   │
              └───────────────┬───────────────┘
                              │
              ┌───────────────▼───────────────┐
              │   HTTP 协议层 + 静态资源路由     │
              │   (Web UI / metrics.html)      │
              └───────────────┬───────────────┘
                              │
         ┌────────────────────┼────────────────────┐
         ▼                    ▼                    ▼
  ┌──────────────┐   ┌──────────────┐   ┌──────────────────┐
  │  鉴权中间件   │   │  API Handlers │   │   断点续传         │
  │ (JWT + 白名单)│   │ 上传/下载/删除│   │  (分片上传/合并)   │
  └──────┬───────┘   └──────┬───────┘   └────────┬─────────┘
         │                  │                     │
         ▼                  ▼                     ▼
  ┌──────────────┐   ┌──────────────┐   ┌──────────────────┐
  │   PostgreSQL  │   │   对象存储    │   │  Redis 缓存层      │
  │  (元数据)     │   │ (SHA-256寻址) │   │ (秒传/分片会话)   │
  └──────────────┘   └──────────────┘   └──────────────────┘
```

## 技术栈

| 类别 | 技术选型 |
|------|----------|
| 网络模型 | 自研 Reactor（Epoll + EventLoop + 线程池） |
| HTTP 解析 | 自研状态机 |
| 对象存储 | 本地磁盘，Content-Addressable（SHA-256），4MB 分块 |
| 元数据 | PostgreSQL + 连接池（libpqxx 6.x） |
| 缓存 | Redis（hiredis） |
| 鉴权 | JWT（HMAC-SHA256，自实现） |
| 配置 | YAML（yaml-cpp） |
| 日志 | spdlog（控制台 + 滚动文件） |
| 监控 | Prometheus 文本格式 `/metrics` |
| 构建 | CMake + GCC/Clang |
| 部署 | Docker 多阶段构建 + docker compose |
| C++ 标准 | C++17 |

---

## 功能特性

### 已实现

| 功能 | 说明 |
|------|------|
| **Web 控制台** | 登录/注册、上传/下载/删除、文件列表（`/`） |
| **监控面板** | 可视化 Prometheus 指标（`/metrics.html`） |
| **文件上传/下载/删除** | REST API + 前端 UI |
| **秒传去重** | SHA-256 哈希比对 + Redis 缓存 |
| **断点续传** | 分片上传（4MB/片），支持暂停/恢复/状态查询 |
| **文件分享** | 生成分享链接，支持密码、过期时间与下载次数限制 |
| **用户注册登录** | JWT token 鉴权，默认 7 天有效期 |
| **鉴权中间件** | 白名单（健康检查、认证、静态资源、分享下载、/metrics） |
| **YAML 配置** | `config/config.yaml` / `config.local.yaml` |
| **结构化日志** | spdlog，级别与文件路径可配置 |
| **Prometheus 监控** | `GET /metrics`，请求数/错误数/流量/连接数 |
| **Docker 部署** | 一键启动 PostgreSQL + Redis + SolarDrive |

### 规划中

| 功能 | 计划 |
|------|------|
| HTTPS | Nginx 反向代理 + Let's Encrypt |
| Grafana 仪表盘 | 对接 `/metrics` 端点 |
| 缩略图生成 | libvips 异步处理 |
| CDN 加速 | 集成对象存储 CDN |

---

## 快速开始

### 方式一：Docker 一键部署（推荐）

**前置条件**：已安装 Docker 与 Docker Compose。

```bash
git clone <your-repo-url> SolarDrive
cd SolarDrive

# 构建并启动（PostgreSQL + Redis + SolarDrive）
docker compose up -d --build

# 查看状态
docker compose ps -a
docker compose logs solardrive --tail 30

# 健康检查
curl http://127.0.0.1:8080/api/v1/health
```

**访问地址（直连 :8080）：**

| 页面 | URL |
|------|-----|
| Web 云盘 | http://127.0.0.1:8080/ |
| 监控面板 | http://127.0.0.1:8080/metrics.html |
| Prometheus 原始指标 | http://127.0.0.1:8080/metrics |
| 健康检查 | http://127.0.0.1:8080/api/v1/health |

> 当前直连 `:8080`；上线 HTTPS 时可再加 Nginx 反代。

**停止服务：**

```bash
docker compose down        # 停止容器
docker compose down -v     # 停止并清除数据卷（慎用）
```

Docker 环境使用 `config/config.yaml`，数据库主机为 compose 服务名 `postgres`，Redis 为 `redis`。

---

### 方式二：本地开发

#### 依赖安装

```bash
# Ubuntu / Debian
sudo apt update
sudo apt install -y \
    build-essential cmake pkg-config \
    libssl-dev \
    libpqxx-dev libpq-dev \
    libhiredis-dev \
    libyaml-cpp-dev \
    libspdlog-dev libfmt-dev \
    libnlohmann-json3-dev \
    postgresql postgresql-client redis-server

# 创建数据库
sudo systemctl start postgresql redis-server
sudo -u postgres psql -c "CREATE DATABASE solardrive;"
```

#### 编译

```bash
cmake -S . -B build -DSOLAR_BUILD_TESTS=ON
cmake --build build -j$(nproc)
```

#### 单元测试

网络 / HTTP / 认证 / 存储 / WebSocket 层 GTest 位于 `test/`，默认开启（`-DSOLAR_BUILD_TESTS=OFF` 可关闭）：

```bash
cd build && ctest --output-on-failure
# 或分别运行
./test/net_test
./test/http_test
./test/auth_test
./test/storage_test
./test/ws_test
```

#### 配置

本地开发优先使用 `config/config.local.yaml`（数据库/Redis 指向 `127.0.0.1`）。

```bash
# 若 PostgreSQL 密码不是默认值，可用环境变量覆盖
export SOLAR_DB="host=127.0.0.1 port=5432 dbname=solardrive user=postgres password=你的密码"
```

**配置文件说明：**

| 文件 | 用途 |
|------|------|
| `config/config.yaml` | Docker / 生产环境（host=`postgres`/`redis`） |
| `config/config.local.yaml` | 本地开发（host=`127.0.0.1`），存在时自动优先 |

#### 运行

```bash
# 自动选择 config.local.yaml（若存在）
./build/solardrive

# 显式指定配置
./build/solardrive -c config/config.local.yaml

# 命令行覆盖端口
./build/solardrive -c config/config.local.yaml 8080

# 查看帮助
./build/solardrive --help
```

启动成功后日志会打印本机与局域网访问地址，例如：

```
web UI (VM local):     http://127.0.0.1:8080/
metrics UI (VM local): http://127.0.0.1:8080/metrics.html
metrics API (VM local): http://127.0.0.1:8080/metrics
metrics UI (LAN/host): http://192.168.x.x:8080/metrics.html
```

---

## 配置参考

`config/config.yaml` 示例：

```yaml
server:
  port: 8080
  threads: 4

storage:
  base_path: "/tmp/solardrive/objects"
  chunk_size: 4194304          # 4MB

database:
  host: "postgres"             # Docker 用服务名；本地改为 127.0.0.1
  port: 5432
  dbname: "solardrive"
  user: "postgres"
  password: "postgres"
  pool_size: 8

redis:
  host: "redis"
  port: 6379

jwt:
  secret: "change-me-in-production"
  ttl_hours: 168

logging:
  level: "info"                # trace/debug/info/warn/error
  file: "logs/solardrive.log"
  max_size_mb: 10
  max_files: 5
```

**环境变量覆盖（优先级高于 YAML）：**

| 变量 | 说明 |
|------|------|
| `SOLAR_CONFIG` | 默认配置文件路径 |
| `SOLAR_DB` | PostgreSQL 完整连接串 |
| `SOLAR_STORE` | 对象存储根目录 |
| `SOLAR_REDIS_HOST` | Redis 主机 |
| `SOLAR_REDIS_PORT` | Redis 端口 |
| `SOLAR_JWT_SECRET` | JWT 签名密钥 |
| `SOLAR_WEB` | Web 静态资源目录 |

---

## Web 界面

启动服务后，浏览器访问：

- **云盘主页**：http://127.0.0.1:8080/
  - 注册/登录 → 上传文件 → 查看/下载/删除
- **监控面板**：http://127.0.0.1:8080/metrics.html
  - 每 5 秒自动刷新，展示请求数、错误数、流量、活跃连接

---

## API 文档

完整接口说明见 **[docs/API.md](docs/API.md)**，包含：

- 全部端点总览（方法、路径、鉴权要求）
- 请求 / 响应 JSON 字段说明
- curl 示例与错误码对照表

**端点速查：**

| 分类 | 接口 |
|------|------|
| 认证（免鉴权） | `POST /api/v1/auth/register`、`POST /api/v1/auth/login` |
| 文件（需 Token） | `POST /api/v1/upload`、`GET /api/v1/files`、`GET/DELETE /api/v1/files/{id}` |
| 断点续传（需 Token） | `POST /api/v1/upload/init`、`PUT .../part/{n}`、`GET .../upload/{id}`、`POST .../complete`、`DELETE .../upload/{id}` |
| 文件分享 | `POST /api/v1/share`、`GET /api/v1/shares`、`DELETE /api/v1/share/{token}`、`GET /s/{token}`（公开下载） |
| 系统（免鉴权） | `GET /api/v1/health`、`GET /metrics` |

**快速示例：**

```bash
# 登录获取 Token
curl -X POST http://localhost:8080/api/v1/auth/login \
  -H "Content-Type: application/json" \
  -d '{"username":"alice","password":"pass123"}'

# 带 Token 上传文件
curl -X POST http://localhost:8080/api/v1/upload \
  -H "Authorization: Bearer <token>" \
  -H "X-File-Name: report.pdf" \
  --data-binary @report.pdf
```

<details>
<summary>展开：README 内嵌 API 详情（与 docs/API.md 同步）</summary>

### 认证接口（免鉴权）

```bash
# 注册
curl -X POST http://localhost:8080/api/v1/auth/register \
  -H "Content-Type: application/json" \
  -d '{"username":"alice","password":"pass123"}'
# → {"user_id":"uuid","username":"alice","token":"eyJ..."}

# 登录
curl -X POST http://localhost:8080/api/v1/auth/login \
  -H "Content-Type: application/json" \
  -d '{"username":"alice","password":"pass123"}'
```

### 文件操作（需要鉴权）

```bash
TOKEN="<your-jwt-token>"

curl -X POST http://localhost:8080/api/v1/upload \
  -H "Authorization: Bearer $TOKEN" \
  -H "X-File-Name: report.pdf" \
  --data-binary @report.pdf

curl http://localhost:8080/api/v1/files -H "Authorization: Bearer $TOKEN"

curl http://localhost:8080/api/v1/files/<file_id> \
  -H "Authorization: Bearer $TOKEN" -o report.pdf

curl -X DELETE http://localhost:8080/api/v1/files/<file_id> \
  -H "Authorization: Bearer $TOKEN"
```

### 断点续传

```bash
curl -X POST http://localhost:8080/api/v1/upload/init \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"file_name":"video.mp4","total_size":1073741824}'

curl -X PUT "http://localhost:8080/api/v1/upload/<upload_id>/part/0" \
  -H "Authorization: Bearer $TOKEN" --data-binary @part0.bin

curl "http://localhost:8080/api/v1/upload/<upload_id>" \
  -H "Authorization: Bearer $TOKEN"

curl -X POST "http://localhost:8080/api/v1/upload/<upload_id>/complete" \
  -H "Authorization: Bearer $TOKEN"
```

### 监控与健康检查

```bash
curl http://localhost:8080/api/v1/health
curl http://localhost:8080/metrics
```

</details>

---

## 项目结构

```
SolarDrive/
├── src/
│   ├── main.cpp                  # 入口：配置加载、路由、鉴权、监控
│   ├── network/                  # Reactor 网络层 + spdlog 封装
│   ├── http/                     # HTTP 解析、路由、静态文件
│   ├── auth/                     # JWT、鉴权中间件、用户 DAO
│   ├── cache/                    # Redis 客户端
│   ├── config/                   # YAML 配置加载
│   ├── monitor/                  # Prometheus 指标收集
│   ├── storage/                  # 对象存储（SHA-256 寻址）
│   ├── metadata/                 # PostgreSQL 连接池 + 文件 DAO
│   └── api/                      # 上传/下载/认证/断点续传/分享 Handler
├── test/
│   ├── net_test/                 # Reactor 网络层
│   ├── http_test/                # HTTP 解析 / 路由 / 静态文件
│   ├── auth_test/                # JWT / 鉴权中间件
│   ├── storage_test/             # 对象存储
│   └── ws_test/                  # WebSocket 编解码 / 握手
├── web/                          # 前端静态资源
│   ├── index.html                # 云盘 Web UI
│   ├── metrics.html              # 监控面板
│   ├── app.js / metrics.js
│   └── style.css
├── config/
│   ├── config.yaml               # Docker / 生产配置
│   └── config.local.yaml         # 本地开发配置
├── deploy/
│   ├── entrypoint.sh             # Docker 启动脚本
│   └── env.example               # Compose 环境变量模板
├── docs/
│   └── API.md                    # API 接口文档
├── docker-compose.yml
├── Dockerfile
└── CMakeLists.txt
```

---

## 常见问题

### curl 连接被拒绝（`Failed to connect to 127.0.0.1:8080`）

1. **服务未启动**：确认进程或容器在运行
   ```bash
   pgrep -a solardrive          # 本地
   docker compose ps -a         # Docker
   sudo lsof -i :8080
   ```
2. **Docker 构建失败**：查看构建日志，重新 `docker compose up -d --build`
3. **VMware 虚拟机环境**：浏览器可能通过端口转发访问，终端 curl 需在**同一环境**执行；或使用日志中打印的 `LAN/host` IP：
   ```bash
   curl http://192.168.x.x:8080/api/v1/health
   ```

### Docker 构建 apt 包找不到

Ubuntu 22.04 运行时包名示例：`libspdlog1`（不是 `libspdlog1.9`）、`libpqxx-6.4`、`libyaml-cpp0.7`。详见 `Dockerfile`。

### PostgreSQL 连接失败

- 本地：检查密码是否与 `config.local.yaml` 一致，或用 `SOLAR_DB` 覆盖
- Docker：确认 `postgres` 容器健康（`docker compose ps`）

---

## 设计亮点

- **Content-Addressable 存储**：文件以 SHA-256 为地址，自动去重
- **两级目录散列**：`/data/ab/cd/<fullhash>` 避免单目录 inode 爆炸
- **连接级 HttpParser**：每个 TCP 连接独立解析器
- **鉴权白名单**：静态资源、健康检查、/metrics 免 token
- **配置分层**：YAML 默认值 + 环境变量覆盖 + 命令行端口

---

## 开发路线图

```
Phase 1 ✅  核心链路：HTTP 解析 + 上传下载 + 元数据
Phase 2 ✅  安全与体验：JWT 鉴权 + Redis 缓存 + 断点续传 + Web UI
Phase 3 ✅  生产化：YAML 配置 + spdlog 日志 + Docker + Prometheus 监控
Phase 4 🔄  功能扩展：文件分享 + HTTPS + Grafana + 缩略图
```

---

## License

MIT
