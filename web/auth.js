/**
 * SolarDrive 登录 / 注册页（login.html）
 */

const statusPill = $('#status-pill');
const statusText = $('#status-text');
const loginForm = $('#login-form');
const registerForm = $('#register-form');

function switchAuthTab(tab) {
  $$('.auth-tab').forEach((btn) => {
    btn.classList.toggle('active', btn.dataset.tab === tab);
  });
  loginForm.hidden = tab !== 'login';
  registerForm.hidden = tab !== 'register';
}

async function login(username, password) {
  const data = await apiRequest(`${API}/auth/login`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ username, password }),
  });
  saveSession(data);
  toast(`欢迎回来，${data.username}`);
  location.replace(resolveRedirectTarget());
}

async function register(username, password) {
  const data = await apiRequest(`${API}/auth/register`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ username, password }),
  });
  saveSession(data);
  toast(`注册成功，${data.username}`);
  location.replace(resolveRedirectTarget());
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

checkHealth({ pill: statusPill, text: statusText });
setInterval(() => {
  checkHealth({ pill: statusPill, text: statusText });
}, 15000);

if (loadSession()) {
  location.replace(resolveRedirectTarget());
}
