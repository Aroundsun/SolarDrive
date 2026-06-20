/**
 * SolarDrive 分享下载页（share.html）
 *
 * 从 URL 解析 share token，调用 /s/{token} 下载接口。
 * 无密码分享自动尝试下载；有密码则展示表单。
 * 旧链接 /s/{token} 会重定向到本页。
 */

const shareLoading = $('#share-loading');
const shareForm = $('#share-form');
const shareError = $('#share-error');
const shareSuccess = $('#share-success');
const shareHint = $('#share-hint');
const passwordInput = $('#share-password');
const downloadForm = $('#share-download-form');
const downloadBtn = $('#share-download-btn');

/** 从 query ?token= 或路径 /s/{token} 提取分享短码 */
function getShareTokenFromUrl() {
  const params = new URLSearchParams(window.location.search);
  const fromQuery = params.get('token');
  if (fromQuery) return fromQuery.trim();

  const match = window.location.pathname.match(/^\/s\/([^/]+)$/);
  return match ? match[1] : '';
}

function showError(msg) {
  shareLoading.hidden = true;
  shareForm.hidden = true;
  shareSuccess.hidden = true;
  shareError.hidden = false;
  shareError.textContent = msg;
}

function showForm(hint) {
  shareLoading.hidden = true;
  shareError.hidden = true;
  shareSuccess.hidden = true;
  shareForm.hidden = false;
  if (hint) shareHint.textContent = hint;
}

function showSuccess(msg) {
  shareLoading.hidden = true;
  shareForm.hidden = true;
  shareError.hidden = true;
  shareSuccess.hidden = false;
  shareSuccess.textContent = msg;
}

/** 请求分享下载 API（X-Share-Download 触发附件下载，计入下载次数） */
async function downloadShare(password) {
  const token = getShareTokenFromUrl();
  if (!token) {
    throw new Error('分享链接无效，缺少 token');
  }

  const qs = password ? `?password=${encodeURIComponent(password)}` : '';
  const res = await fetch(`/s/${encodeURIComponent(token)}${qs}`, {
    headers: {
      'X-Share-Download': '1',
      Accept: 'application/octet-stream',
    },
  });

  if (!res.ok) {
    throw new Error(await parseErrorResponse(res));
  }

  const blob = await res.blob();
  const disp = res.headers.get('Content-Disposition') || '';
  const match = disp.match(/filename="([^"]+)"/);
  const name = match ? match[1] : 'download';

  triggerBlobDownload(blob, name);
  return name;
}

function triggerBlobDownload(blob, name) {
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = name;
  a.click();
  URL.revokeObjectURL(url);
}

/** 进入页面时尝试无密码直接下载 */
async function tryAutoDownload() {
  try {
    const name = await downloadShare('');
    showSuccess(`已开始下载：${name}`);
    toast(`下载开始：${name}`);
  } catch (err) {
    const msg = err.message || '下载失败';
    if (msg.includes('password')) {
      showForm('此分享需要密码，请输入后点击下载');
      passwordInput.focus();
    } else {
      showError(msg);
    }
  }
}

downloadForm.addEventListener('submit', async (e) => {
  e.preventDefault();
  downloadBtn.disabled = true;
  try {
    const name = await downloadShare(passwordInput.value);
    showSuccess(`已开始下载：${name}`);
    toast(`下载开始：${name}`);
  } catch (err) {
    toast(err.message || '下载失败', 'err');
    if ((err.message || '').includes('password')) {
      showForm('密码错误或需要密码，请重新输入');
      passwordInput.focus();
    } else {
      showError(err.message || '下载失败');
    }
  } finally {
    downloadBtn.disabled = false;
  }
});

// ---- 页面初始化 ----
const token = getShareTokenFromUrl();
const urlParams = new URLSearchParams(window.location.search);
const urlPassword = urlParams.get('password') || '';

if (!token) {
  showError('分享链接无效，请检查链接是否完整');
} else if (window.location.pathname.startsWith('/s/')) {
  const extra = urlParams.toString();
  window.location.replace(`/share.html?token=${encodeURIComponent(token)}${extra ? `&${extra}` : ''}`);
} else if (urlPassword) {
  passwordInput.value = urlPassword;
  downloadShare(urlPassword)
    .then((name) => {
      showSuccess(`已开始下载：${name}`);
      toast(`下载开始：${name}`);
    })
    .catch((err) => {
      const msg = err.message || '下载失败';
      if (msg.includes('password')) {
        showForm('密码错误或需要密码，请重新输入');
      } else {
        showError(msg);
      }
    });
} else {
  tryAutoDownload();
}
