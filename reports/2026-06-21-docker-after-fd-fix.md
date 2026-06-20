# SolarDrive Docker 压测报告（FD 泄漏修复后）

> **报告 ID**: `2026-06-21-docker-after-fd-fix`  
> **测试时间**: 2026-06-21  
> **对比基线**: [`2026-06-21-docker-baseline.md`](2026-06-21-docker-baseline.md)  
> **用途**: TCP 连接 / FD 泄漏修复后的性能与稳定性对比

---

## 1. 本次修复内容

| 改动 | 文件 | 说明 |
|------|------|------|
| HTTP 响应后主动关连接 | `http_response.cpp/h` | 新增 `send_response_and_close()`，写完后 `force_close()` |
| 打破 `shared_ptr` 循环引用 | `main.cpp` | HttpParser 回调改用 `weak_ptr<TcpConnection>` |
| 连接销毁时释放 FD | `tcp_connection.cpp` | `connection_destroyed()` 清理 `context_` 并 `socket_.reset()` |
| WS 错误响应统一关连接 | `ws_upgrade.cpp` | 错误路径走 `send_response_and_close()` |

**根因回顾**：

1. `Connection: close` 响应发出后未主动关闭，FD 长期占用  
2. `TcpConnection` ↔ `HttpParser` 循环引用，连接无法销毁  
3. 容器 `ulimit -n=1024`，约 992 个泄漏 FD 后服务僵死  

---

## 2. 测试环境

与基线报告一致：

| 项目 | 值 |
|------|-----|
| 部署方式 | `docker compose`（solardrive + postgres + redis） |
| 目标地址 | `http://127.0.0.1:8080` |
| IO 线程 | 4 |
| DB 连接池 | 8 |
| HTTP 模式 | `Connection: close`（短连接，写完后服务端主动关闭） |
| 容器 FD 上限 | 1024 |
| 压测工具 | Python 可控并发脚本、curl 顺序压测 |

---

## 3. 压测方法

```
1. 重启 solardrive 容器（修复后镜像）
2. FD 验证     → 顺序 curl health × 1500，观察 /proc/1/fd 数量
3. 并发压测   → Python bench，c=50，duration=15s
4. 压测后检查 → health、metrics（active_connections / requests_total）
```

---

## 4. 测试结果

### 4.1 FD 泄漏验证（顺序 1500 次 health）

| 指标 | 结果 |
|------|------|
| 成功请求 | **1500 / 1500** |
| 失败请求 | **0** |
| 压测后 health | **OK** |
| 进程 FD 数（`/proc/1/fd`） | **30**（基线水平，未增长） |
| active_connections | **1** |

### 4.2 并发压测 — `GET /api/v1/health`

| 参数 | 值 |
|------|-----|
| 并发 | 50 |
| 时长 | 15s |
| 超时 | 5s |

| 指标 | 结果 |
|------|------|
| 完成请求 | 10,745 |
| 成功 | **10,745** |
| 失败 | **0** |
| QPS | **714.07** |
| 延迟 avg | 46.61 ms |
| 延迟 p50 | 42.24 ms |
| 延迟 p90 | 82.16 ms |
| 延迟 p99 | 126.63 ms |
| 延迟 max | 199.22 ms |
| 压测后 health | **OK** |
| active_connections | **1** |
| requests_total（累计） | 12,284 |

---

## 5. 与基线对比

| 维度 | 基线（修复前） | 修复后 | 变化 |
|------|----------------|--------|------|
| 顺序请求上限 | **第 993 次失败** | **1500+ 成功** | ✅ 稳定性恢复 |
| 进程 FD（~1000 次请求后） | **~1024（耗尽）** | **~30** | ✅ 无泄漏 |
| c=50 并发 15s QPS | **~66–99 后僵死** | **714** | ✅ ~7–10× |
| 并发错误率 | 100%（僵死后） | **0%** | ✅ |
| 压测后服务可用 | ❌ 空响应 / 僵死 | ✅ health OK | ✅ |
| active_connections（压测后） | 不可用 | **1** | ✅ |
| 1000 连接长压 | ❌ 未完成 | ⚠️ 未测（建议后续补 wrk c=1000） | — |

### 基线 vs 修复后 QPS（有效窗口内）

| 场景 | 基线 QPS | 修复后 QPS | 备注 |
|------|----------|------------|------|
| health c=10, 15s | ~66（约 992 次后挂） | — | 基线受 FD 上限截断 |
| health c=50, 15s | 0（僵死） | **714** | 修复后可完整跑满 |
| 顺序 health | ~99（10s 窗口） | 稳定 1500+ | FD 不再累积 |

---

## 6. 结论

1. **FD 泄漏已修复**：1500 次顺序请求后 FD 仍为 ~30，服务正常。  
2. **稳定性恢复**：压测后 health 可用，`active_connections` 回到 1。  
3. **吞吐显著提升**：c=50 下 QPS **714**，零错误；基线在同场景下无法完成测试。  
4. **后续建议**：  
   - 补测 wrk `c=1000` 长连接场景  
   - 可选：容器 `ulimit -n` 提高到 65535（防御性）  
   - 可选：HTTP Keep-Alive（进一步降低握手开销）

---

## 7. 复现命令

```bash
# 构建并启动
sg docker -c "docker compose up -d --build solardrive"

# FD 验证（1500 次顺序请求）
for i in $(seq 1 1500); do
  curl -sf --max-time 2 http://127.0.0.1:8080/api/v1/health >/dev/null || { echo "fail at $i"; break; }
done
docker exec solardrive-solardrive-1 ls /proc/1/fd | wc -l

# 并发压测（需 Python bench 脚本）
python3 http_bench.py http://127.0.0.1:8080/api/v1/health -c 50 -d 15 -t 5

# metrics
curl -s http://127.0.0.1:8080/metrics | grep -E 'requests_total|active_connections|errors_total'
```

---

*本报告对应 FD 泄漏修复提交，与 `2026-06-21-docker-baseline.md` 配对使用。*
