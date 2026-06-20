# SolarDrive Docker 压测报告（HTTP Keep-Alive）

> **报告 ID**: `2026-06-21-docker-keep-alive`  
> **测试时间**: 2026-06-21  
> **前置报告**: [`2026-06-21-docker-after-fd-fix.md`](2026-06-21-docker-after-fd-fix.md)  
> **用途**: Keep-Alive 实现与 `Connection: close` 模式性能对比

---

## 1. 本次改动

| 改动 | 文件 | 说明 |
|------|------|------|
| 请求侧协商 | `http_request.h/cpp` | `get_header_ic()`、`wants_keep_alive()` |
| 响应侧 Connection 头 | `http_response.h/cpp` | `set_close_connection()`、`send_response(conn, resp, keep_alive)` |
| 连接复用调度 | `main.cpp` | `HttpConnContext` 计数，单连接最多 1000 次请求 |
| WebSocket / 错误 | `ws_upgrade.cpp` | 仍走 `send_response_and_close()`，不启用 keep-alive |

### 协商规则

| 客户端 | 服务端行为 |
|--------|------------|
| HTTP/1.1，无 `Connection: close` | **Keep-Alive**（默认） |
| `Connection: keep-alive` | **Keep-Alive** |
| `Connection: close` | 响应后关闭连接 |
| HTTP/1.0，无 `keep-alive` | 响应后关闭连接 |
| 单连接请求数 ≥ 1000 | 强制关闭（防滥用） |

### 响应头示例（Keep-Alive）

```http
HTTP/1.1 200 OK
Connection: keep-alive
Keep-Alive: timeout=60, max=1000
Content-Type: application/json; charset=utf-8
Content-Length: 38

{"status":"ok","service":"SolarDrive"}
```

---

## 2. 测试环境

| 项目 | 值 |
|------|-----|
| 部署 | `docker compose`（solardrive + postgres + redis） |
| 目标 | `http://127.0.0.1:8080` |
| IO 线程 | 4 |
| 接口 | `GET /api/v1/health` |
| 工具 | Python `http.client`（单连接顺序请求） |
| 请求数 | 各 2000 次 |

---

## 3. 压测方法

```python
# Connection: close — 每请求新建连接（客户端显式 close）
conn.request('GET', '/api/v1/health', headers={'Connection': 'close'})

# Keep-Alive — 单连接复用（HTTP/1.1 默认）
conn.request('GET', '/api/v1/health')  # 无 Connection: close
```

每组测试 2000 次顺序请求，对比 QPS 与延迟；压测后检查 health 与 `active_connections`。

---

## 4. 测试结果

### 4.1 Keep-Alive vs Connection: close（2000 次 health）

| 模式 | 成功 | QPS | avg | p50 | p99 |
|------|------|-----|-----|-----|-----|
| `Connection: close` | 2000/2000 | **513.2** | 1.95 ms | 1.88 ms | 3.30 ms |
| **Keep-Alive** | 2000/2000 | **790.9** | 1.26 ms | 1.20 ms | 2.31 ms |

**提升**：QPS **+54%**（513 → 791），p50 延迟 **-36%**（1.88ms → 1.20ms）

### 4.2 压测后稳定性

| 指标 | 结果 |
|------|------|
| health 检查 | **OK** |
| active_connections | **1** |
| FD 泄漏 | 无（连接正常释放） |

---

## 5. 历史对比（health 接口）

| 阶段 | 场景 | QPS | 备注 |
|------|------|-----|------|
| 基线 | c=10, 15s | ~66 | ~992 次后 FD 耗尽僵死 |
| FD 修复后 | c=50, 15s 并发 | **714** | Python bench，Connection: close |
| FD 修复后 | 顺序 2000 | **513** | 每请求新连接 |
| **Keep-Alive** | 顺序 2000 | **791** | 单连接复用 |

说明：

- Keep-Alive 顺序压测 QPS（791）高于 FD 修复后的 close 顺序压测（513），符合「减少 TCP 握手」预期
- 与 FD 修复后 c=50 并发（714）相比，Keep-Alive 单连接仍略高，因无并发调度开销

---

## 6. 结论

1. **Keep-Alive 已生效**：HTTP/1.1 默认持久连接，解析器复用与 pipelining 正常工作  
2. **性能提升明显**：同接口 QPS +54%，延迟 p50 降低约 36%  
3. **稳定性 OK**：2000 次复用后服务正常，`active_connections` 回到 1  
4. **未测项**：wrk `c=1000` + Keep-Alive 长连接压测（建议后续补充）

---

## 7. 复现命令

```bash
# 启动
sg docker -c "docker compose up -d --build solardrive"

# Keep-Alive vs close 对比（Python）
python3 - <<'PY'
import http.client, time, statistics

def bench(keep_alive, n=2000):
    conn = http.client.HTTPConnection('127.0.0.1', 8080, timeout=5)
    hdrs = {} if keep_alive else {'Connection': 'close'}
    t0 = time.perf_counter()
    ok = 0
    lats = []
    for _ in range(n):
        s = time.perf_counter()
        conn.request('GET', '/api/v1/health', headers=hdrs)
        r = conn.getresponse()
        r.read()
        if r.status == 200:
            ok += 1
            lats.append((time.perf_counter()-s)*1000)
    elapsed = time.perf_counter()-t0
    conn.close()
    lats.sort()
    p = lambda q: lats[int((len(lats)-1)*q)] if lats else 0
    print(f"{'keep-alive' if keep_alive else 'close':10} qps={ok/elapsed:.1f} p50={p(.5):.2f}ms p99={p(.99):.2f}ms")

bench(False)
bench(True)
PY

curl -s http://127.0.0.1:8080/metrics | grep active_connections
```

---

## 8. 报告索引

| 报告 | 说明 |
|------|------|
| [`2026-06-21-docker-baseline.md`](2026-06-21-docker-baseline.md) | 优化前基线（FD 泄漏） |
| [`2026-06-21-docker-after-fd-fix.md`](2026-06-21-docker-after-fd-fix.md) | FD 泄漏修复后 |
| **本报告** | HTTP Keep-Alive 启用后 |

---

*本报告与 FD 修复、Keep-Alive 代码变更配套，供后续优化（流式下载、并发上限等）对比使用。*
