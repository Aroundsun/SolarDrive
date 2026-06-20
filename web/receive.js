/**
 * SolarDrive 接收分享页（receive.html）
 * 粘贴链接或短码，支持打开预览、保存到网盘、下载到本地
 */

const openShareInput = $('#open-share-input');
const openSharePassword = $('#open-share-password');
const openShareOpenBtn = $('#open-share-open-btn');
const openShareSaveBtn = $('#open-share-save-btn');
const openShareDownloadBtn = $('#open-share-download-btn');
const openShareStatus = $('#open-share-status');

function parseShareToken(raw) {
  const text = String(raw || '').trim();
  if (!text) return '';

  if (/^[a-z0-9]{8}$/i.test(text)) {
    return text.toLowerCase();
  }

  try {
    const url = text.startsWith('http') ? new URL(text) : new URL(text, window.location.origin);
    const fromQuery = url.searchParams.get('token');
    if (fromQuery) return fromQuery.trim().toLowerCase();

    const pathMatch = url.pathname.match(/\/s\/([a-z0-9]{8})$/i);
    if (pathMatch) return pathMatch[1].toLowerCase();
  } catch {
    /* 非 URL，继续尝试路径匹配 */
  }

  const pathMatch = text.match(/\/s\/([a-z0-9]{8})/i);
  if (pathMatch) return pathMatch[1].toLowerCase();

  const tokenMatch = text.match(/[?&]token=([a-z0-9]{8})/i);
  if (tokenMatch) return tokenMatch[1].toLowerCase();

  return '';
}

function getOpenShareParams() {
  const token = parseShareToken(openShareInput.value);
  const password = openSharePassword.value.trim();
  if (!token) {
    toast('无法识别分享链接，请粘贴完整 URL 或 8 位短码', 'err');
    return null;
  }
  return { token, password };
}

function setOpenShareStatus(text, type = '') {
  if (!text) {
    openShareStatus.hidden = true;
    openShareStatus.textContent = '';
    openShareStatus.className = 'open-share-status';
    return;
  }
  openShareStatus.hidden = false;
  openShareStatus.textContent = text;
  openShareStatus.className = `open-share-status ${type}`.trim();
}

async function fetchShareBlob(token, password, mode = 'download') {
  const qs = password ? `?password=${encodeURIComponent(password)}` : '';
  const headers = {};
  if (mode === 'download') {
    headers['X-Share-Download'] = '1';
    headers.Accept = 'application/octet-stream';
  } else if (mode === 'preview') {
    headers['X-Share-Preview'] = '1';
    headers.Accept = 'text/html,application/pdf,image/*,video/*,audio/*,text/*,*/*';
  }

  const res = await fetch(`/s/${encodeURIComponent(token)}${qs}`, { headers });

  if (!res.ok) {
    throw new Error(await parseErrorResponse(res));
  }

  const blob = await res.blob();
  const disp = res.headers.get('Content-Disposition') || '';
  const match = disp.match(/filename="([^"]+)"/);
  const name = match ? match[1] : 'download';
  const mime = res.headers.get('Content-Type') || blob.type || 'application/octet-stream';
  return { blob, name, mime };
}

function canPreviewMime(mime) {
  if (!mime) return false;
  return /^(image\/|video\/|audio\/|text\/|application\/pdf)/i.test(mime);
}

function triggerBlobDownload(blob, name) {
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = name;
  a.click();
  URL.revokeObjectURL(url);
}

async function openSharePreview(token, password) {
  setOpenShareStatus('正在加载预览…');
  openShareOpenBtn.disabled = true;
  try {
    const { blob, name, mime } = await fetchShareBlob(token, password, 'preview');
    if (!canPreviewMime(mime)) {
      setOpenShareStatus('此文件类型不支持预览，请使用下载', 'err');
      toast('此文件类型不支持预览，请使用下载', 'err');
      return;
    }
    const url = URL.createObjectURL(blob);
    const win = window.open(url, '_blank', 'noopener');
    if (!win) {
      URL.revokeObjectURL(url);
      toast('无法打开新窗口，请检查浏览器弹窗拦截', 'err');
      setOpenShareStatus('无法打开预览窗口', 'err');
      return;
    }
    setTimeout(() => URL.revokeObjectURL(url), 120000);
    setOpenShareStatus(`已打开预览：${name}`, 'ok');
  } catch (err) {
    const msg = err.message || '预览失败';
    setOpenShareStatus(msg, 'err');
    toast(msg, 'err');
  } finally {
    openShareOpenBtn.disabled = false;
  }
}

async function downloadOpenShare() {
  const params = getOpenShareParams();
  if (!params) return;

  setOpenShareStatus('正在下载…');
  openShareDownloadBtn.disabled = true;
  try {
    const { blob, name } = await fetchShareBlob(params.token, params.password);
    triggerBlobDownload(blob, name);
    setOpenShareStatus(`已开始下载：${name}`, 'ok');
    toast(`下载开始：${name}`);
  } catch (err) {
    const msg = err.message || '下载失败';
    setOpenShareStatus(msg, 'err');
    toast(msg, 'err');
  } finally {
    openShareDownloadBtn.disabled = false;
  }
}

async function saveOpenShare() {
  const params = getOpenShareParams();
  if (!params) return;

  if (!getToken()) {
    const redirect = `/receive.html?token=${encodeURIComponent(params.token)}`;
    location.href = `/login.html?redirect=${encodeURIComponent(redirect)}`;
    return;
  }

  setOpenShareStatus('正在保存到网盘…');
  openShareSaveBtn.disabled = true;
  try {
    const data = await apiRequest(`${API}/share/save`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        share_token: params.token,
        password: params.password,
      }),
    });
    setOpenShareStatus(`已保存到「我的文件」：${data.name}`, 'ok');
    toast(`已保存：${data.name}`);
  } catch (err) {
    const msg = err.message || '保存失败';
    setOpenShareStatus(msg, 'err');
    toast(msg, 'err');
  } finally {
    openShareSaveBtn.disabled = false;
  }
}

async function openShareOpen() {
  const params = getOpenShareParams();
  if (!params) return;
  await openSharePreview(params.token, params.password);
}

openShareOpenBtn.addEventListener('click', openShareOpen);
openShareSaveBtn.addEventListener('click', saveOpenShare);
openShareDownloadBtn.addEventListener('click', downloadOpenShare);

// 从 URL 预填 token / password（便于登录后跳回）
(function initFromQuery() {
  const params = new URLSearchParams(location.search);
  const token = params.get('token');
  const password = params.get('password');
  if (token) {
    openShareInput.value = token;
  }
  if (password) {
    openSharePassword.value = password;
  }
})();
