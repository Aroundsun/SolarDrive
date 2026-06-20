const API = '/api/v1';
const TOKEN_KEY = 'solardrive_token';
const USER_KEY = 'solardrive_user';

const $ = (sel) => document.querySelector(sel);
const $$ = (sel) => document.querySelectorAll(sel);

const appRoot = $('#app');
const statusPill = $('#status-pill');
const statusText = $('#status-text');
const authCard = $('#auth-card');
const mainPanel = $('#main-panel');
const userBar = $('#user-bar');
const userNameEl = $('#user-name');
const loginForm = $('#login-form');
const registerForm = $('#register-form');
const dropzone = $('#dropzone');
const fileInput = $('#file-input');
const uploadQueue = $('#upload-queue');
const filesTable = $('#files-table');
const filesBody = $('#files-body');
const filesEmpty = $('#files-empty');
const refreshBtn = $('#refresh-btn');
const footerStats = $('#footer-stats');
const toastStack = $('#toast-stack');

let currentUser = null;

function toast(message, type = 'ok') {
  const el = document.createElement('div');
  el.className = `toast ${type}`;
  el.textContent = message;
  toastStack.appendChild(el);
  setTimeout(() => el.remove(), 3200);
}

function getToken() {
  return localStorage.getItem(TOKEN_KEY) || '';
}

function saveSession(data) {
  localStorage.setItem(TOKEN_KEY, data.token);
  localStorage.setItem(USER_KEY, JSON.stringify({
    user_id: data.user_id,
    username: data.username,
  }));
  currentUser = { user_id: data.user_id, username: data.username };
}

function clearSession() {
  localStorage.removeItem(TOKEN_KEY);
  localStorage.removeItem(USER_KEY);
  currentUser = null;
}

function loadSession() {
  const token = getToken();
  const raw = localStorage.getItem(USER_KEY);
  if (!token || !raw) return false;
  try {
    currentUser = JSON.parse(raw);
    return true;
  } catch {
    clearSession();
    return false;
  }
}

function authHeaders(extra = {}) {
  const headers = { ...extra };
  const token = getToken();
  if (token) {
    headers.Authorization = `Bearer ${token}`;
  }
  return headers;
}

function formatSize(bytes) {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  if (bytes < 1024 * 1024 * 1024) return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
  return `${(bytes / 1024 / 1024 / 1024).toFixed(2)} GB`;
}

function formatTime(iso) {
  if (!iso) return '—';
  const d = new Date(iso);
  if (Number.isNaN(d.getTime())) return iso;
  return d.toLocaleString('zh-CN', {
    year: 'numeric',
    month: '2-digit',
    day: '2-digit',
    hour: '2-digit',
    minute: '2-digit',
  });
}

function escapeHtml(str) {
  return String(str)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

async function apiRequest(url, options = {}) {
  const { skipAuth = false, logoutOn401 = true, json = true, ...fetchOpts } = options;
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), 15000);

  const headers = skipAuth
    ? { ...(fetchOpts.headers || {}) }
    : authHeaders(fetchOpts.headers || {});

  try {
    const res = await fetch(url, { ...fetchOpts, headers, signal: ctrl.signal });
    if (!res.ok) {
      const text = await res.text();
      let msg = res.statusText;
      try {
        const err = JSON.parse(text);
        if (err.error) msg = err.error;
      } catch { /* ignore */ }

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
  } finally {
    clearTimeout(timer);
  }
}

function handleUnauthorized() {
  clearSession();
  showAuthView();
  toast('登录已过期，请重新登录', 'err');
}

function showAuthView() {
  appRoot.dataset.view = 'auth';
  authCard.hidden = false;
  mainPanel.hidden = true;
  userBar.hidden = true;
  filesBody.innerHTML = '';
  uploadQueue.innerHTML = '';
}

function showAppView() {
  appRoot.dataset.view = 'app';
  authCard.hidden = true;
  mainPanel.hidden = false;
  userBar.hidden = false;
  userNameEl.textContent = currentUser ? currentUser.username : '';
}

function switchAuthTab(tab) {
  $$('.auth-tab').forEach((btn) => {
    btn.classList.toggle('active', btn.dataset.tab === tab);
  });
  loginForm.hidden = tab !== 'login';
  registerForm.hidden = tab !== 'register';
}

async function checkHealth() {
  try {
    const data = await apiRequest(`${API}/health`, { skipAuth: true });
    statusPill.classList.add('online');
    statusPill.classList.remove('offline');
    statusText.textContent = data.service || '在线';
  } catch {
    statusPill.classList.add('offline');
    statusPill.classList.remove('online');
    statusText.textContent = '服务离线';
  }
}

async function login(username, password) {
  const data = await apiRequest(`${API}/auth/login`, {
    method: 'POST',
    skipAuth: true,
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ username, password }),
  });
  saveSession(data);
  showAppView();
  toast(`欢迎回来，${data.username}`);
  try {
    await loadFiles({ logoutOn401: false });
  } catch (err) {
    toast(`加载文件列表失败: ${err.message}`, 'err');
  }
}

async function register(username, password) {
  const data = await apiRequest(`${API}/auth/register`, {
    method: 'POST',
    skipAuth: true,
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ username, password }),
  });
  saveSession(data);
  showAppView();
  toast(`注册成功，${data.username}`);
  try {
    await loadFiles({ logoutOn401: false });
  } catch (err) {
    toast(`加载文件列表失败: ${err.message}`, 'err');
  }
}

function logout() {
  clearSession();
  showAuthView();
  toast('已退出登录');
}

async function loadFiles(options = {}) {
  if (!getToken()) return;
  try {
    const data = await apiRequest(`${API}/files`, options);
    renderFiles(data.files || []);
  } catch (err) {
    toast(`加载文件列表失败: ${err.message}`, 'err');
  }
}

function renderFiles(files) {
  filesBody.innerHTML = '';
  footerStats.textContent = `${files.length} 个文件`;

  if (files.length === 0) {
    filesTable.hidden = true;
    filesEmpty.hidden = false;
    return;
  }

  filesEmpty.hidden = true;
  filesTable.hidden = false;

  for (const file of files) {
    const tr = document.createElement('tr');

    const nameTd = document.createElement('td');
    nameTd.innerHTML = `
      <div class="file-name">
        <strong>${escapeHtml(file.name)}</strong>
        <span class="file-id">${escapeHtml(file.id)}</span>
      </div>`;

    const sizeTd = document.createElement('td');
    sizeTd.textContent = formatSize(file.size);

    const timeTd = document.createElement('td');
    timeTd.textContent = formatTime(file.created_at);

    const actionsTd = document.createElement('td');
    actionsTd.className = 'actions';

    const downloadBtn = document.createElement('button');
    downloadBtn.type = 'button';
    downloadBtn.className = 'btn btn-primary';
    downloadBtn.textContent = '下载';
    downloadBtn.addEventListener('click', () => downloadFile(file));

    const copyBtn = document.createElement('button');
    copyBtn.type = 'button';
    copyBtn.className = 'btn btn-ghost';
    copyBtn.textContent = '复制 ID';
    copyBtn.addEventListener('click', () => copyId(file.id));

    const deleteBtn = document.createElement('button');
    deleteBtn.type = 'button';
    deleteBtn.className = 'btn btn-danger';
    deleteBtn.textContent = '删除';
    deleteBtn.addEventListener('click', () => deleteFile(file));

    actionsTd.append(downloadBtn, copyBtn, deleteBtn);
    tr.append(nameTd, sizeTd, timeTd, actionsTd);
    filesBody.appendChild(tr);
  }
}

async function copyId(id) {
  try {
    await navigator.clipboard.writeText(id);
    toast('已复制 file_id');
  } catch {
    toast('复制失败', 'err');
  }
}

async function downloadFile(file) {
  try {
    const res = await apiRequest(
      `${API}/files/${encodeURIComponent(file.id)}`,
      { json: false }
    );
    const blob = await res.blob();
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = file.name || 'download';
    a.click();
    URL.revokeObjectURL(url);
    toast(`开始下载 ${file.name}`);
  } catch (err) {
    toast(`下载失败: ${err.message}`, 'err');
  }
}

async function deleteFile(file) {
  if (!confirm(`确定删除「${file.name}」吗？`)) return;

  try {
    await apiRequest(`${API}/files/${encodeURIComponent(file.id)}`, {
      method: 'DELETE',
      json: false,
    });
    toast(`已删除 ${file.name}`);
    await loadFiles();
  } catch (err) {
    toast(`删除失败: ${err.message}`, 'err');
  }
}

function createUploadItem(name) {
  const item = document.createElement('div');
  item.className = 'upload-item';
  item.innerHTML = `
    <div class="upload-item-name">${escapeHtml(name)}</div>
    <div class="upload-item-meta">准备上传…</div>
    <div class="progress-bar"><span></span></div>
  `;
  uploadQueue.prepend(item);
  return item;
}

async function uploadFile(file) {
  const item = createUploadItem(file.name);
  const meta = item.querySelector('.upload-item-meta');
  const bar = item.querySelector('.progress-bar > span');

  bar.style.width = '30%';

  try {
    const res = await apiRequest(`${API}/upload`, {
      method: 'POST',
      json: false,
      headers: {
        'X-File-Name': encodeURIComponent(file.name),
        'Content-Type': file.type || 'application/octet-stream',
      },
      body: file,
    });

    bar.style.width = '100%';
    const data = await res.json();

    item.classList.add('ok');
    const tag = data.instant ? '秒传' : '上传完成';
    meta.textContent = `${tag} · ${formatSize(data.size)} · ${data.file_id.slice(0, 8)}…`;
    toast(`${file.name} ${tag}`);
    await loadFiles();
  } catch (err) {
    item.classList.add('err');
    meta.textContent = `失败: ${err.message}`;
    toast(`${file.name} 上传失败`, 'err');
  }
}

async function handleFiles(fileList) {
  if (!getToken()) {
    toast('请先登录', 'err');
    return;
  }
  const files = Array.from(fileList || []);
  for (const file of files) {
    await uploadFile(file);
  }
}

$$('.auth-tab').forEach((btn) => {
  btn.addEventListener('click', () => switchAuthTab(btn.dataset.tab));
});

loginForm.addEventListener('submit', async (e) => {
  e.preventDefault();
  const username = $('#login-username').value.trim();
  const password = $('#login-password').value;
  const submitBtn = loginForm.querySelector('button[type="submit"]');
  submitBtn.disabled = true;
  try {
    await login(username, password);
  } catch (err) {
    toast(`登录失败: ${err.message}`, 'err');
  } finally {
    submitBtn.disabled = false;
  }
});

registerForm.addEventListener('submit', async (e) => {
  e.preventDefault();
  const username = $('#register-username').value.trim();
  const password = $('#register-password').value;
  const submitBtn = registerForm.querySelector('button[type="submit"]');
  submitBtn.disabled = true;
  try {
    await register(username, password);
  } catch (err) {
    toast(`注册失败: ${err.message}`, 'err');
  } finally {
    submitBtn.disabled = false;
  }
});

$('#logout-btn').addEventListener('click', logout);

fileInput.addEventListener('change', (e) => {
  handleFiles(e.target.files);
  fileInput.value = '';
});

dropzone.addEventListener('dragover', (e) => {
  e.preventDefault();
  dropzone.classList.add('dragover');
});

dropzone.addEventListener('dragleave', () => {
  dropzone.classList.remove('dragover');
});

dropzone.addEventListener('drop', (e) => {
  e.preventDefault();
  dropzone.classList.remove('dragover');
  handleFiles(e.dataTransfer.files);
});

refreshBtn.addEventListener('click', loadFiles);

checkHealth();
setInterval(checkHealth, 15000);

if (loadSession()) {
  showAppView();
  loadFiles({ logoutOn401: false }).catch(() => {
    clearSession();
    showAuthView();
    toast('登录已过期，请重新登录', 'err');
  });
} else {
  showAuthView();
}
