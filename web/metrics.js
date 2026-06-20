/**
 * SolarDrive Prometheus 监控面板（metrics.html）
 * 优先通过 WebSocket /ws/metrics 接收推送，失败时降级为轮询 /metrics
 */

const REFRESH_MS = 5000;

const METRIC_META = {
  solardrive_requests_total: {
    title: 'HTTP 请求总数',
    desc: '累计处理的 HTTP 请求次数',
    type: 'counter',
    format: 'number',
  },
  solardrive_errors_total: {
    title: 'HTTP 错误总数',
    desc: '返回 4xx / 5xx 的响应次数',
    type: 'counter',
    format: 'number',
    danger: true,
  },
  solardrive_upload_bytes_total: {
    title: '上传流量',
    desc: '累计上传字节数',
    type: 'counter',
    format: 'bytes',
  },
  solardrive_download_bytes_total: {
    title: '下载流量',
    desc: '累计下载字节数',
    type: 'counter',
    format: 'bytes',
  },
  solardrive_active_connections: {
    title: '活跃连接',
    desc: '当前在线 TCP 连接数',
    type: 'gauge',
    format: 'number',
  },
};

const statusPill = $('#status-pill');
const statusText = $('#status-text');
const metricsGrid = $('#metrics-grid');
const updatedAt = $('#updated-at');
const rawMetrics = $('#raw-metrics');
const refreshBtn = $('#refresh-btn');
const toggleRawBtn = $('#toggle-raw-btn');

let pollTimer = null;
let metricsWs = null;
let useWebSocket = false;

function formatNumber(value) {
  return Number(value).toLocaleString('zh-CN');
}

function formatValue(name, value) {
  const meta = METRIC_META[name];
  if (!meta) return String(value);
  if (meta.format === 'bytes') return formatBytes(value);
  return formatNumber(value);
}

/** 解析 Prometheus 文本格式为 { metric_name: value } */
function parsePrometheus(text) {
  const metrics = {};
  const lines = text.split('\n');

  for (const line of lines) {
    const trimmed = line.trim();
    if (!trimmed || trimmed.startsWith('#')) continue;

    const space = trimmed.lastIndexOf(' ');
    if (space <= 0) continue;

    const name = trimmed.slice(0, space).trim();
    const value = trimmed.slice(space + 1).trim();
    if (name && value !== '') {
      metrics[name] = value;
    }
  }

  return metrics;
}

function metricsFromJson(data) {
  const metrics = {};
  for (const name of Object.keys(METRIC_META)) {
    if (data[name] !== undefined) {
      metrics[name] = String(data[name]);
    }
  }
  return metrics;
}

function setStatus(ok, message) {
  statusPill.classList.toggle('offline', !ok);
  statusText.textContent = message;
}

function renderMetrics(metrics, rawText) {
  const order = Object.keys(METRIC_META);
  const cards = order.map((name) => {
    const meta = METRIC_META[name];
    const value = metrics[name] ?? '0';
    const dangerClass = meta.danger && Number(value) > 0 ? ' metric-danger' : '';

    return `
      <article class="metric-card card${dangerClass}">
        <div class="metric-card-head">
          <h3>${meta.title}</h3>
          <span class="metric-type">${meta.type}</span>
        </div>
        <p class="metric-value">${formatValue(name, value)}</p>
        <p class="metric-desc">${meta.desc}</p>
        <p class="metric-key"><code>${name}</code></p>
      </article>
    `;
  }).join('');

  metricsGrid.innerHTML = cards;
  rawMetrics.textContent = rawText;
  updatedAt.textContent = new Date().toLocaleString('zh-CN');
}

function handleMetricsPayload(metrics, rawText) {
  renderMetrics(metrics, rawText);
  setStatus(true, useWebSocket ? 'WebSocket 推送正常' : '指标正常');
}

async function loadMetrics() {
  try {
    const res = await fetch('/metrics', { cache: 'no-store' });
    if (!res.ok) {
      throw new Error(`HTTP ${res.status}`);
    }

    const text = await res.text();
    const metrics = parsePrometheus(text);
    handleMetricsPayload(metrics, text);
  } catch (err) {
    setStatus(false, `加载失败: ${err.message}`);
    metricsGrid.innerHTML = `
      <article class="metric-card card metric-danger">
        <h3>无法获取指标</h3>
        <p class="metric-desc">请确认 SolarDrive 服务已启动，且当前页面与 API 在同一地址访问。</p>
      </article>
    `;
  }
}

function startPolling() {
  if (pollTimer) clearInterval(pollTimer);
  pollTimer = setInterval(loadMetrics, REFRESH_MS);
}

function stopPolling() {
  if (pollTimer) {
    clearInterval(pollTimer);
    pollTimer = null;
  }
}

function connectMetricsWs() {
  const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
  const ws = new WebSocket(`${proto}//${location.host}/ws/metrics`);

  ws.onopen = () => {
    useWebSocket = true;
    stopPolling();
    setStatus(true, 'WebSocket 已连接');
  };

  ws.onmessage = (event) => {
    try {
      const data = JSON.parse(event.data);
      if (data.type !== 'metrics') return;
      const metrics = metricsFromJson(data);
      handleMetricsPayload(metrics, JSON.stringify(data, null, 2));
    } catch {
      // 忽略解析失败
    }
  };

  ws.onerror = () => {
    useWebSocket = false;
    if (ws.readyState !== WebSocket.CLOSED) {
      ws.close();
    }
    loadMetrics();
    startPolling();
    setStatus(false, 'WebSocket 不可用，已降级为轮询');
  };

  ws.onclose = () => {
    if (useWebSocket) {
      useWebSocket = false;
      loadMetrics();
      startPolling();
      setStatus(false, 'WebSocket 已断开，已降级为轮询');
    }
    metricsWs = null;
    setTimeout(connectMetricsWs, REFRESH_MS);
  };

  metricsWs = ws;
}

refreshBtn.addEventListener('click', loadMetrics);

toggleRawBtn.addEventListener('click', () => {
  const show = rawMetrics.hidden;
  rawMetrics.hidden = !show;
  toggleRawBtn.textContent = show ? '收起' : '展开';
});

connectMetricsWs();
loadMetrics();
