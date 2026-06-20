/**
 * SolarDrive 云盘主界面逻辑
 *
 * 模块：文件上传下载、分享创建与管理
 * 登录见 login.html；接收分享见 receive.html
 */

// ---- DOM 引用 ----
const statusPill = $('#status-pill');
const statusText = $('#status-text');
const mainPanel = $('#main-panel');
const userBar = $('#user-bar');
const userNameEl = $('#user-name');
const dropzone = $('#dropzone');
const fileInput = $('#file-input');
const uploadQueue = $('#upload-queue');
const filesTable = $('#files-table');
const filesBody = $('#files-body');
const filesEmpty = $('#files-empty');
const filesLoading = $('#files-loading');
const filesCard = $('.files-card');
const folderBreadcrumb = $('#folder-breadcrumb');
const newFolderBtn = $('#new-folder-btn');
const uploadFolderHint = $('#upload-folder-hint');
const refreshBtn = $('#refresh-btn');
const sharesBody = $('#shares-body');
const sharesTable = $('#shares-table');
const sharesEmpty = $('#shares-empty');
const sharesLoading = $('#shares-loading');
const refreshSharesBtn = $('#refresh-shares-btn');
const shareModal = $('#share-modal');
const shareUrlInput = $('#share-url-input');
const shareModalHint = $('#share-modal-hint');
const shareCopyBtn = $('#share-copy-btn');
const shareCloseBtn = $('#share-close-btn');
const footerStats = $('#footer-stats');

let currentFolderId = null;
let folderPath = [{ id: null, name: '全部文件' }];

function initPageIcons() {
  $('#upload-section-icon').innerHTML = icon('upload', 'icon-sm');
  $('#files-section-icon').innerHTML = icon('folder', 'icon-sm');
  $('#shares-section-icon').innerHTML = icon('share', 'icon-sm');
  $('#drop-icon').innerHTML = icon('upload', 'icon-lg');
  $('#files-empty-icon').innerHTML = icon('folder', 'icon-lg');
  $('#shares-empty-icon').innerHTML = icon('link', 'icon-lg');
  $('#user-icon').innerHTML = icon('user', 'icon-sm');
  setButtonIcon($('#receive-link'), 'receive', '接收分享');
  setButtonIcon($('#logout-btn'), 'logout', '退出');
  setButtonIcon(newFolderBtn, 'plus', '新建文件夹');
  setButtonIcon(refreshBtn, 'refresh', '刷新');
  setButtonIcon(refreshSharesBtn, 'refresh', '刷新');
}

function setPanelLoading(panelEl, loadingEl, cardEl, loading) {
  if (loadingEl) loadingEl.hidden = !loading;
  if (cardEl) cardEl.classList.toggle('is-loading', loading);
  if (panelEl) panelEl.style.pointerEvents = loading ? 'none' : '';
}

function renderBreadcrumb() {
  folderBreadcrumb.innerHTML = '';
  folderPath.forEach((item, index) => {
    if (index > 0) {
      const sep = document.createElement('span');
      sep.className = 'breadcrumb-sep';
      sep.innerHTML = icon('chevronRight', 'icon-sm');
      folderBreadcrumb.appendChild(sep);
    }

    const btn = document.createElement('button');
    btn.type = 'button';
    btn.className = 'breadcrumb-item';
    if (index === folderPath.length - 1) {
      btn.classList.add('is-current');
    }
    btn.innerHTML = `${index === 0 ? icon('home', 'icon-sm') : icon('folder', 'icon-sm')}<span>${escapeHtml(item.name)}</span>`;
    btn.addEventListener('click', () => {
      if (index === folderPath.length - 1) return;
      navigateToFolder(item.id, folderPath.slice(0, index + 1));
    });
    folderBreadcrumb.appendChild(btn);
  });

  const current = folderPath[folderPath.length - 1];
  uploadFolderHint.textContent = current.id
    ? `上传到「${current.name}」`
    : '上传到根目录';
}

function navigateToFolder(folderId, path = null) {
  currentFolderId = folderId || null;
  if (path) {
    folderPath = path;
  }
  renderBreadcrumb();
  loadFiles();
}

function folderQuery(key, value) {
  if (!value) return '';
  return `?${key}=${encodeURIComponent(value)}`;
}

function uploadExtraHeaders() {
  return currentFolderId ? { 'X-Folder-Id': currentFolderId } : {};
}

function createIconButton(label, iconName, className = 'btn btn-ghost btn-sm btn-icon') {
  const btn = document.createElement('button');
  btn.type = 'button';
  btn.className = className;
  btn.title = label;
  setButtonIcon(btn, iconName, label);
  return btn;
}

function initAppView() {
  const user = getUser();
  userNameEl.textContent = user ? user.username : '';
  initPageIcons();
  renderBreadcrumb();
  loadShares({ logoutOn401: false }).catch(() => {});
}

function showShareModal(url, hasPassword) {
  shareUrlInput.value = url;
  shareModalHint.textContent = hasPassword
    ? '链接已含密码保护，请将链接和密码一并发送给对方'
    : '复制下方短链发送给他人即可下载';
  shareModal.hidden = false;
  setTimeout(() => {
    shareUrlInput.focus();
    shareUrlInput.select();
  }, 0);
}

function hideShareModal() {
  shareModal.hidden = true;
}

async function copyShareUrl() {
  await copyText(shareUrlInput.value, shareUrlInput);
}

// ---- 我的文件：文件夹 + 列表 ----
async function loadFiles(options = {}) {
  if (!getToken()) return;
  setPanelLoading(filesTable, filesLoading, filesCard, true);
  try {
    const [folderData, fileData] = await Promise.all([
      apiRequest(`${API}/folders${folderQuery('parent_id', currentFolderId)}`, options),
      apiRequest(`${API}/files${folderQuery('folder_id', currentFolderId)}`, options),
    ]);
    renderBrowse(folderData.folders || [], fileData.files || []);
  } catch (err) {
    toast(`加载文件列表失败: ${err.message}`, 'err');
  } finally {
    setPanelLoading(filesTable, filesLoading, filesCard, false);
  }
}

function logout() {
  clearSession();
  redirectToLogin();
}

function renderBrowse(folders, files) {
  filesBody.innerHTML = '';
  const total = folders.length + files.length;
  footerStats.textContent = `${total} 项 · ${folderPath[folderPath.length - 1].name}`;

  if (total === 0) {
    filesTable.hidden = true;
    filesEmpty.hidden = false;
    return;
  }

  filesEmpty.hidden = true;
  filesTable.hidden = false;

  for (const folder of folders) {
    const tr = document.createElement('tr');
    tr.className = 'folder-row';

    const nameTd = document.createElement('td');
    nameTd.innerHTML = `
      <div class="file-row-main">
        <div class="file-row-icon folder">${icon('folder')}</div>
        <div class="file-name">
          <strong>${escapeHtml(folder.name)}</strong>
          <span class="file-id">文件夹</span>
        </div>
      </div>`;

    const sizeTd = document.createElement('td');
    sizeTd.textContent = '—';

    const timeTd = document.createElement('td');
    timeTd.textContent = formatTime(folder.created_at);

    const actionsTd = document.createElement('td');
    actionsTd.className = 'actions';

    const openBtn = createIconButton('打开', 'folder');
    openBtn.addEventListener('click', (e) => {
      e.stopPropagation();
      enterFolder(folder);
    });

    const renameBtn = createIconButton('重命名', 'edit');
    renameBtn.addEventListener('click', (e) => {
      e.stopPropagation();
      renameFolder(folder);
    });

    const deleteBtn = createIconButton('删除', 'trash', 'btn btn-danger btn-sm btn-icon');
    deleteBtn.addEventListener('click', (e) => {
      e.stopPropagation();
      deleteFolder(folder);
    });

    actionsTd.append(openBtn, renameBtn, deleteBtn);
    tr.append(nameTd, sizeTd, timeTd, actionsTd);
    tr.addEventListener('click', () => enterFolder(folder));
    filesBody.appendChild(tr);
  }

  for (const file of files) {
    const tr = document.createElement('tr');

    const nameTd = document.createElement('td');
    nameTd.innerHTML = `
      <div class="file-row-main">
        <div class="file-row-icon">${icon('file')}</div>
        <div class="file-name">
          <strong>${escapeHtml(file.name)}</strong>
          <span class="file-id">${escapeHtml(file.id.slice(0, 8))}…</span>
        </div>
      </div>`;

    const sizeTd = document.createElement('td');
    sizeTd.textContent = formatSize(file.size);

    const timeTd = document.createElement('td');
    timeTd.textContent = formatTime(file.created_at);

    const actionsTd = document.createElement('td');
    actionsTd.className = 'actions';

    const downloadBtn = createIconButton('下载', 'download', 'btn btn-primary btn-sm btn-icon');
    downloadBtn.addEventListener('click', () => downloadFile(file));

    const shareBtn = createIconButton('分享', 'share');
    shareBtn.addEventListener('click', () => shareFile(file));

    const copyBtn = createIconButton('复制 ID', 'copy');
    copyBtn.addEventListener('click', () => copyId(file.id));

    const deleteBtn = createIconButton('删除', 'trash', 'btn btn-danger btn-sm btn-icon');
    deleteBtn.addEventListener('click', () => deleteFile(file));

    actionsTd.append(downloadBtn, shareBtn, copyBtn, deleteBtn);
    tr.append(nameTd, sizeTd, timeTd, actionsTd);
    filesBody.appendChild(tr);
  }
}

function enterFolder(folder) {
  folderPath.push({ id: folder.id, name: folder.name });
  navigateToFolder(folder.id, folderPath);
}

async function createFolderPrompt() {
  const name = prompt('请输入文件夹名称');
  if (!name || !name.trim()) return;

  try {
    await apiRequest(`${API}/folders`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        name: name.trim(),
        parent_id: currentFolderId,
      }),
    });
    toast('文件夹已创建');
    await loadFiles();
  } catch (err) {
    toast(`创建失败: ${err.message}`, 'err');
  }
}

async function renameFolder(folder) {
  const name = prompt('重命名文件夹', folder.name);
  if (!name || !name.trim() || name.trim() === folder.name) return;

  try {
    await apiRequest(`${API}/folders/${encodeURIComponent(folder.id)}`, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ name: name.trim() }),
    });
    toast('已重命名');
    await loadFiles();
  } catch (err) {
    toast(`重命名失败: ${err.message}`, 'err');
  }
}

async function deleteFolder(folder) {
  if (!confirm(`确定删除文件夹「${folder.name}」吗？\n（需为空文件夹）`)) return;

  try {
    await apiRequest(`${API}/folders/${encodeURIComponent(folder.id)}`, {
      method: 'DELETE',
      json: false,
    });
    toast('文件夹已删除');
    await loadFiles();
  } catch (err) {
    toast(`删除失败: ${err.message}`, 'err');
  }
}

async function copyId(id) {
  await copyText(id);
}

async function shareFile(file) {
  const password = prompt('分享密码（留空表示无密码）') ?? '';
  if (password === null) return;

  const hoursRaw = prompt('有效时长（小时，留空表示永不过期）', '24');
  if (hoursRaw === null) return;

  const body = { file_id: file.id };
  if (password.trim()) body.password = password.trim();
  const hours = parseInt(hoursRaw, 10);
  if (!Number.isNaN(hours) && hours > 0) body.expires_in_hours = hours;

  try {
    const data = await apiRequest(`${API}/share`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    });
    const url = `${window.location.origin}${data.url}`;
    showShareModal(url, data.has_password);
    await copyText(url, shareUrlInput);
    toast(`短链已生成：${data.share_token}`);
    await loadShares();
  } catch (err) {
    toast(`创建分享失败: ${err.message}`, 'err');
  }
}

// ---- 我的分享列表 ----
async function loadShares(options = {}) {
  if (!getToken()) return;
  setPanelLoading(sharesTable, sharesLoading, $('.shares-card'), true);
  try {
    const shares = await apiRequest(`${API}/shares`, options);
    renderShares(Array.isArray(shares) ? shares : []);
  } catch (err) {
    toast(`加载分享列表失败: ${err.message}`, 'err');
  } finally {
    setPanelLoading(sharesTable, sharesLoading, $('.shares-card'), false);
  }
}

function renderShares(shares) {
  sharesBody.innerHTML = '';

  if (shares.length === 0) {
    sharesTable.hidden = true;
    sharesEmpty.hidden = false;
    return;
  }

  sharesEmpty.hidden = true;
  sharesTable.hidden = false;

  for (const share of shares) {
    const tr = document.createElement('tr');
    const url = `${window.location.origin}${share.url}`;

    const linkTd = document.createElement('td');
    linkTd.innerHTML = `
      <div class="file-name">
        <strong class="share-link">${escapeHtml(share.url)}</strong>
        <span class="file-id">${share.has_password ? '需密码' : '公开'}</span>
      </div>`;

    const fileTd = document.createElement('td');
    fileTd.textContent = share.file_id.slice(0, 8) + '…';

    const countTd = document.createElement('td');
    const max = share.max_downloads > 0 ? share.max_downloads : '∞';
    countTd.textContent = `${share.download_count} / ${max}`;

    const actionsTd = document.createElement('td');
    actionsTd.className = 'actions';

    const copyBtn = createIconButton('复制短链', 'copy');
    copyBtn.addEventListener('click', () => copyText(url));

    const revokeBtn = createIconButton('撤销', 'trash', 'btn btn-danger btn-sm btn-icon');
    revokeBtn.addEventListener('click', () => revokeShare(share.share_token));

    actionsTd.append(copyBtn, revokeBtn);
    tr.append(linkTd, fileTd, countTd, actionsTd);
    sharesBody.appendChild(tr);
  }
}

async function revokeShare(token) {
  if (!confirm(`确定撤销分享 ${token} 吗？`)) return;
  try {
    await apiRequest(`${API}/share/${encodeURIComponent(token)}`, {
      method: 'DELETE',
      json: false,
    });
    toast('分享已撤销');
    await loadShares();
  } catch (err) {
    toast(`撤销失败: ${err.message}`, 'err');
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
    <div class="upload-item-name">${icon('file', 'icon-sm')}<span>${escapeHtml(name)}</span></div>
    <div class="upload-item-meta">准备上传…</div>
    <div class="progress-bar"><span class="is-active"></span></div>
  `;
  uploadQueue.prepend(item);
  return item;
}

// ---- 文件上传：小文件直传，大文件分片 + WebSocket 进度 ----
const MULTIPART_THRESHOLD = 4 * 1024 * 1024;

function connectUploadWs(uploadId, onMessage) {
  const token = getToken();
  const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
  const ws = new WebSocket(
    `${proto}//${location.host}/ws/upload/${uploadId}?token=${encodeURIComponent(token)}`
  );
  ws.onmessage = (event) => {
    try {
      onMessage(JSON.parse(event.data));
    } catch {
      // 忽略非 JSON 帧
    }
  };
  return ws;
}

async function uploadFileMultipart(file, item, meta, bar) {
  const initData = await apiRequest(`${API}/upload/init`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      file_name: file.name,
      total_size: file.size,
      mime_type: file.type || 'application/octet-stream',
      folder_id: currentFolderId,
    }),
  });

  const { upload_id: uploadId, chunk_size: chunkSize, chunk_count: chunkCount } = initData;
  const ws = connectUploadWs(uploadId, (msg) => {
    if (msg.type === 'progress') {
      bar.style.width = `${msg.percent}%`;
      bar.classList.add('is-active');
      meta.textContent = `上传中 ${msg.percent}% · ${formatSize(msg.bytes_uploaded)}/${formatSize(msg.total_size)}`;
    } else if (msg.type === 'complete') {
      bar.style.width = '100%';
      bar.classList.remove('is-active');
    }
  });

  try {
    for (let part = 0; part < chunkCount; part += 1) {
      const start = part * chunkSize;
      const end = Math.min(start + chunkSize, file.size);
      const chunk = file.slice(start, end);

      await apiRequest(`${API}/upload/${uploadId}/part/${part}`, {
        method: 'PUT',
        json: false,
        headers: { 'Content-Type': 'application/octet-stream' },
        body: chunk,
      });
    }

    const data = await apiRequest(`${API}/upload/${uploadId}/complete`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: '{}',
    });

    bar.style.width = '100%';
    bar.classList.remove('is-active');
    item.classList.add('ok');
    meta.textContent = `上传完成 · ${formatSize(data.size)} · ${data.file_id.slice(0, 8)}…`;
    toast(`${file.name} 上传完成`);
    await loadFiles();
  } finally {
    if (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING) {
      ws.close();
    }
  }
}

async function uploadFile(file) {
  const item = createUploadItem(file.name);
  const meta = item.querySelector('.upload-item-meta');
  const bar = item.querySelector('.progress-bar > span');

  try {
    if (file.size > MULTIPART_THRESHOLD) {
      meta.textContent = '分片上传中…';
      await uploadFileMultipart(file, item, meta, bar);
      return;
    }

    bar.style.width = '30%';
    const res = await apiRequest(`${API}/upload`, {
      method: 'POST',
      json: false,
      headers: {
        ...uploadExtraHeaders(),
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
    bar.classList.remove('is-active');
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

// ---- 事件绑定与启动 ----
$('#logout-btn').addEventListener('click', logout);
newFolderBtn.addEventListener('click', createFolderPrompt);

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
refreshSharesBtn.addEventListener('click', loadShares);
shareCopyBtn.addEventListener('click', copyShareUrl);
shareCloseBtn.addEventListener('click', hideShareModal);
shareModal.addEventListener('click', (e) => {
  if (e.target === shareModal) hideShareModal();
});

checkHealth({ pill: statusPill, text: statusText, showLoading: true });
setInterval(() => {
  checkHealth({ pill: statusPill, text: statusText, showLoading: true });
}, 15000);

if (!loadSession()) {
  redirectToLogin();
} else {
  initAppView();
  loadFiles({ logoutOn401: false }).catch(() => {
    clearSession();
    redirectToLogin();
  });
}
