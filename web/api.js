/**
 * SolarDrive HTTP API 封装（依赖 session.js、ui.js 可选用于 toast）
 */

const API = '/api/v1';
const API_TIMEOUT_MS = 15000;

async function parseErrorResponse(res) {
  const text = await res.text();
  let msg = res.statusText;
  try {
    const err = JSON.parse(text);
    if (err.error) msg = err.error;
  } catch {
    /* ignore */
  }
  return msg;
}

async function apiRequest(url, options = {}) {
  const {
    skipAuth = false,
    logoutOn401 = true,
    json = true,
    timeoutMs = API_TIMEOUT_MS,
    ...fetchOpts
  } = options;

  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), timeoutMs);

  const headers = skipAuth
    ? { ...(fetchOpts.headers || {}) }
    : authHeaders(fetchOpts.headers || {});

  try {
    const res = await fetch(url, { ...fetchOpts, headers, signal: ctrl.signal });
    if (!res.ok) {
      const msg = await parseErrorResponse(res);
      if (res.status === 401 && !skipAuth && logoutOn401) {
        handleUnauthorized();
      }
      throw new Error(msg);
    }

    if (!json) return res;
    const ct = res.headers.get('Content-Type') || '';
    if (ct.includes('application/json')) {
      return res.json();
    }
    return res;
  } catch (err) {
    if (err.name === 'AbortError') {
      throw new Error('请求超时，请重试');
    }
    throw err;
  } finally {
    clearTimeout(timer);
  }
}

/**
 * @param {{ pill?: Element, text?: Element, showLoading?: boolean }} opts
 */
async function checkHealth(opts = {}) {
  const { pill, text, showLoading = false } = opts;
  if (!pill || !text) return;

  if (showLoading) pill.classList.add('loading');
  try {
    const data = await apiRequest(`${API}/health`, { skipAuth: true });
    pill.classList.add('online');
    pill.classList.remove('offline', 'loading');
    text.textContent = data.service || '在线';
  } catch {
    pill.classList.add('offline');
    pill.classList.remove('online', 'loading');
    text.textContent = '服务离线';
  }
}
