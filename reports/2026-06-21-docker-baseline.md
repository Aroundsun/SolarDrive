# SolarDrive Docker 压测报告（基线）

> **报告 ID**: `2026-06-21-docker-baseline`  
> **测试时间**: 2026-06-21  
> **用途**: 优化前基线，供后续对比

---

## 1. 测试环境

| 项目 | 值 |
|------|-----|
| 部署方式 | `docker compose`（solardrive + postgres + redis） |
| 目标地址 | `http://127.0.0.1:8080`（宿主机映射） |
| 压测网络 | Docker 内 `http://solardrive:8080`（wrk bench 容器） |
| SolarDrive 配置 | `config/config.yaml` |
| IO 线程 | 4 |
| DB 连接池 | 8 |
| HTTP 模式 | `Connection: close` |
| 容器 FD 上限 | **1024**（`ulimit -n`） |
| 压测工具 | wrk 4.1.0、Python 可控并发脚本 |

### 相关 Git 状态（测试时）

- 分支: `main`
- 说明: WebSocket / 分享 / Schema v2 等改动已合入本地

---

## 2. 压测方法

```
1. 基线检查     → GET /api/v1/health、/metrics
2. 隔离压测     → 每项前 restart solardrive 容器，避免前项拖垮服务
3. wrk 对比     → bench 容器内 wrk，c=10, d=10s
4. FD 上限验证  → 顺序 curl health 直到失败
5. 单请求延迟   → health / files(JWT) / login 各 20 次顺序请求
```

---

## 3. 吞吐量结果（服务僵死前有效窗口 ≈ 992 次请求）

| 场景 | 并发 | 时长 | QPS | p50 | p90 | p99 | max |
|------|------|------|-----|-----|-----|-----|-----|
| `GET /api/v1/health` | 5 | 10s | **99.2** | 4.3ms | 6.9ms | 13.7ms | 42.7ms |
| `GET /api/v1/health` | 10 | 15s | **66.1** | 8.1ms | 15.3ms | 29.1ms | 71.2ms |
| `GET /api/v1/health` | 20 | 15s | **66.1** | 19.4ms | 36.4ms | 54.8ms | 75.5ms |
| `GET /` 静态首页 | 10 | 10s | **99.0** | 8.4ms | 14.9ms | 33.0ms | 101.9ms |
| wrk health（Docker 内） | 10 | 10s | **99.1** | 1.0ms | 2.4ms | 15.3ms | — |

**wrk 原始输出（health, c=10, d=10s）:**

```
Running 10s test @ http://solardrive:8080/api/v1/health
  2 threads and 10 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency     1.37ms    1.83ms  17.83ms   96.77%
    Req/Sec     1.66k     1.09k    2.79k    50.00%
  Latency Distribution
     50%    1.02ms
     75%    1.60ms
     90%    2.35ms
     99%   15.31ms
  992 requests in 10.01s, 138.53KB read
  Socket errors: connect 0, read 103293, write 0, timeout 0
Requests/sec:     99.14
Transfer/sec:     13.84KB
```

---

## 4. 单请求延迟（顺序 20 次，无并发）

| 接口 | 平均 | p50 | p99 |
|------|------|-----|-----|
| `GET /api/v1/health` | 2.0ms | 1.9ms | 2.6ms |
| `GET /api/v1/files`（JWT + DB） | 8.8ms | 5.2ms | 21.0ms |
| `POST /api/v1/auth/login` | 4.8ms | 4.7ms | 9.9ms |

---

## 5. 资源占用

### 压测前

| 容器 | CPU | 内存 |
|------|-----|------|
| solardrive | ~0% | ~3.3 MiB |
| postgres | ~1.5% | ~65 MiB |
| redis | ~0.8% | ~33 MiB |

### 压测后（服务僵死时）

| 容器 | CPU | 内存 | 网络 I/O |
|------|-----|------|----------|
| solardrive | ~0%（僵死） | ~10 MiB | 160MB / 112MB |
| postgres | ~7% | ~66 MiB | — |
| redis | ~1% | ~33 MiB | — |

---

## 6. 关键发现：FD 泄漏导致服务僵死

### 现象

- 顺序 `curl` 健康检查在第 **993** 次请求失败
- 并发压测在 **~992** 次成功请求后僵死（TCP 可连，HTTP 空响应）
- 进程仍在（CPU 约 77%），但不再响应
- 高并发 wrk（c=100+）在僵死后 QPS 为 0

### 根因分析

```
992 成功请求 + ~30 基础 FD ≈ 1024（ulimit -n）→ 第 993 次起全面失败
```

- HTTP 响应使用 **`Connection: close`**
- 连接关闭后 **FD 未正确释放**（疑似 tcp_connection 泄漏）
- 容器默认 **`ulimit -n = 1024`** 放大问题

### 验证命令

```bash
# 顺序请求直到失败（实测失败于第 993 次）
for i in $(seq 1 995); do
  curl -sf --max-time 2 http://127.0.0.1:8080/api/v1/health >/dev/null || { echo "failed at $i"; break; }
done

# 查看容器 FD 上限
docker exec solardrive-solardrive-1 bash -c 'ulimit -n; cat /proc/1/limits | grep "open files"'
# Max open files  1024  524288
```

---

## 7. 未完成的测试项

| 测试项 | 状态 | 原因 |
|--------|------|------|
| wrk c=1000 长连接压测 | ❌ 未通过 | ~992 请求后 FD 耗尽，服务僵死 |
| JWT `/api/v1/files`  sustained 压测 | ⚠️ 不完整 | 同上；单请求/低并发正常（p50 ~5ms） |
| 1000 并发连接稳定性 | ❌ 未通过 | FD 泄漏阻断 |

---

## 8. 结论摘要

| 维度 | 基线值 | 备注 |
|------|--------|------|
| health QPS | **66 ~ 99** | 受 FD 上限约束，非稳态上限 |
| 鉴权 + DB 单请求延迟 | p50 **~5ms** | 可接受 |
| 稳定性 | **差** | ~1000 请求后必僵死 |
| 1000 连接压测 | **失败** | FD 泄漏 |

---

## 9. 后续优化建议（对比时关注）

1. **修复 TCP 连接 / FD 泄漏**（`src/network/tcp_connection.cpp` 关闭路径）
2. 容器提高 `ulimit -n`（如 65535，临时缓解）
3. 支持 **HTTP Keep-Alive**（降低握手开销）
4. 实现或临时关闭 `rate_limit_per_ip`（配置存在，代码未 enforce）

---

## 10. 复现命令参考

```bash
# 启动 stack
sg docker -c "docker compose up -d"

# 健康检查
curl -sf http://127.0.0.1:8080/api/v1/health

# wrk（需在 bench 容器或已安装 wrk 的环境）
wrk -t2 -c10 -d10s --latency http://127.0.0.1:8080/api/v1/health

# metrics
curl -s http://127.0.0.1:8080/metrics | grep -E 'requests_total|errors_total|active_connections'
```

---

*本报告由压测会话自动生成，用于 SolarDrive 性能优化前后对比。*
