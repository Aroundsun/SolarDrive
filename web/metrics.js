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

const $ = (sel) => document.querySelector(sel);

const statusPill = $('#status-pill');
const statusText = $('#status-text');
const metricsGrid = $('#metrics-grid');
const updatedAt = $('#updated-at');
const rawMetrics = $('#raw-metrics');
const refreshBtn = $('#refresh-btn');
const toggleRawBtn = $('#toggle-raw-btn');

let timer = null;

function formatNumber(value) {
  return Number(value).toLocaleString('zh-CN');
}

function formatBytes(bytes) {
  const n = Number(bytes);
  if (n < 1024) return `${n} B`;
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KB`;
  if (n < 1024 * 1024 * 1024) return `${(n / (1024 * 1024)).toFixed(2)} MB`;
  return `${(n / (1024 * 1024 * 1024)).toFixed(2)} GB`;
}

function formatValue(name, value) {
  const meta = METRIC_META[name];
  if (!meta) return String(value);
  if (meta.format === 'bytes') return formatBytes(value);
  return formatNumber(value);
}

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

async function loadMetrics() {
  try {
    const res = await fetch('/metrics', { cache: 'no-store' });
    if (!res.ok) {
      throw new Error(`HTTP ${res.status}`);
    }

    const text = await res.text();
    const metrics = parsePrometheus(text);
    renderMetrics(metrics, text);
    setStatus(true, '指标正常');
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

function startAutoRefresh() {
  if (timer) clearInterval(timer);
  timer = setInterval(loadMetrics, REFRESH_MS);
}

refreshBtn.addEventListener('click', loadMetrics);

toggleRawBtn.addEventListener('click', () => {
  const show = rawMetrics.hidden;
  rawMetrics.hidden = !show;
  toggleRawBtn.textContent = show ? '收起' : '展开';
});

loadMetrics();
startAutoRefresh();
