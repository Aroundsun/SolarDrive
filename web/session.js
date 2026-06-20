/**
 * SolarDrive 会话工具（login.html / index.html 共用）
 * JWT 与用户信息存 localStorage
 */

const TOKEN_KEY = 'solardrive_token';
const USER_KEY = 'solardrive_user';

let currentUser = null;

function getToken() {
  return localStorage.getItem(TOKEN_KEY) || '';
}

function getUser() {
  return currentUser;
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

function redirectToLogin() {
  const path = location.pathname + location.search;
  if (path.startsWith('/login.html')) {
    location.href = '/login.html';
    return;
  }
  location.href = `/login.html?redirect=${encodeURIComponent(path || '/')}`;
}

function resolveRedirectTarget() {
  const params = new URLSearchParams(location.search);
  const target = params.get('redirect') || '/';
  if (!target.startsWith('/') || target.startsWith('//')) {
    return '/';
  }
  return target;
}

function handleUnauthorized() {
  clearSession();
  redirectToLogin();
}
