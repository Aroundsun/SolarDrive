#!/usr/bin/env bash
# =============================================================================
# SolarDrive API 冒烟测试
#
# 流程：health → register → login → upload → list → download → share
#       → 公开分享下载 → revoke share → delete
#
# 用法：
#   ./scripts/demo.sh              # 一键启动 + 冒烟
#   docker compose up -d --build   # 或手动启动后：
#   ./scripts/smoke_test.sh
#   BASE_URL=http://192.168.1.100:8080 ./scripts/smoke_test.sh
# =============================================================================

set -euo pipefail

BASE_URL="${BASE_URL:-http://127.0.0.1:8080}"
CURL_MAX_TIME="${CURL_MAX_TIME:-30}"
API="${BASE_URL}/api/v1"
RUN_ID="$(date +%s)_$$"
USERNAME="smoke_${RUN_ID}"
PASSWORD="smoke_pass_${RUN_ID}"
SMOKE_FILE="$(mktemp /tmp/solardrive_smoke_XXXXXX.txt)"
DOWNLOAD_FILE="$(mktemp /tmp/solardrive_smoke_dl_XXXXXX.bin)"
SHARE_DL_FILE="$(mktemp /tmp/solardrive_smoke_share_XXXXXX.bin)"

TOKEN=""
FILE_ID=""
SHARE_TOKEN=""
STEP=0
HTTP_CODE=0
HTTP_BODY=""

cleanup() {
  rm -f "$SMOKE_FILE" "$DOWNLOAD_FILE" "$SHARE_DL_FILE"
}
trap cleanup EXIT

green() { printf '\033[32m%s\033[0m\n' "$*"; }
red()   { printf '\033[31m%s\033[0m\n' "$*" >&2; }

step() {
  STEP=$((STEP + 1))
  printf '\n[%d] %s\n' "$STEP" "$1"
}

die() {
  red "FAIL: $*"
  exit 1
}

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "缺少命令: $1"
}

json_get() {
  local json="$1"
  local key="$2"
  python3 - "$key" "$json" <<'PY'
import json, sys
key, raw = sys.argv[1], sys.argv[2]
data = json.loads(raw)
cur = data
for part in key.split("."):
    if isinstance(cur, list):
        cur = cur[int(part)]
    else:
        cur = cur[part]
if isinstance(cur, bool):
    print("true" if cur else "false")
else:
    print(cur)
PY
}

# 发起 API 请求，结果写入 HTTP_CODE / HTTP_BODY（勿在子 shell 中调用）
api() {
  local method="$1"
  local url="$2"
  shift 2

  local body_file
  body_file="$(mktemp)"

  HTTP_CODE="$(
    curl -sS --max-time "$CURL_MAX_TIME" -X "$method" "$url" "$@" \
      -o "$body_file" \
      -w '%{http_code}'
  )" || {
    rm -f "$body_file"
    die "curl 请求失败: $method $url"
  }

  HTTP_BODY="$(cat "$body_file")"
  rm -f "$body_file"
}

expect_code() {
  local expected="$1"
  [[ "$HTTP_CODE" == "$expected" ]] || die "期望 HTTP $expected，实际 ${HTTP_CODE}；响应: ${HTTP_BODY}"
}

require_cmd curl
require_cmd python3

green "SolarDrive API smoke test"
echo "  BASE_URL = $BASE_URL"
echo "  USER     = $USERNAME"

echo "SolarDrive smoke test payload ${RUN_ID}" >"$SMOKE_FILE"
EXPECTED_CONTENT="$(cat "$SMOKE_FILE")"
UPLOAD_NAME="smoke_${RUN_ID}.txt"

step "GET /api/v1/health"
api GET "${API}/health"
expect_code 200
status="$(json_get "$HTTP_BODY" "status")"
[[ "$status" == "ok" ]] || die "health status 异常: $HTTP_BODY"
green "  health ok"

step "POST /api/v1/auth/register"
api POST "${API}/auth/register" \
  -H "Content-Type: application/json" \
  -d "{\"username\":\"${USERNAME}\",\"password\":\"${PASSWORD}\"}"
expect_code 200
TOKEN="$(json_get "$HTTP_BODY" "token")"
[[ -n "$TOKEN" ]] || die "register 未返回 token: $HTTP_BODY"
green "  registered, token acquired"

step "POST /api/v1/auth/login"
api POST "${API}/auth/login" \
  -H "Content-Type: application/json" \
  -d "{\"username\":\"${USERNAME}\",\"password\":\"${PASSWORD}\"}"
expect_code 200
TOKEN="$(json_get "$HTTP_BODY" "token")"
[[ -n "$TOKEN" ]] || die "login 未返回 token: $HTTP_BODY"
green "  login ok"

step "POST /api/v1/upload"
api POST "${API}/upload" \
  -H "Authorization: Bearer ${TOKEN}" \
  -H "X-File-Name: ${UPLOAD_NAME}" \
  -H "Content-Type: text/plain" \
  --data-binary @"${SMOKE_FILE}"
expect_code 200
FILE_ID="$(json_get "$HTTP_BODY" "file_id")"
[[ -n "$FILE_ID" ]] || die "upload 未返回 file_id: $HTTP_BODY"
green "  uploaded file_id=${FILE_ID}"

step "GET /api/v1/files"
api GET "${API}/files" \
  -H "Authorization: Bearer ${TOKEN}"
expect_code 200
python3 - "$FILE_ID" "$HTTP_BODY" <<'PY'
import json, sys
file_id, raw = sys.argv[1], sys.argv[2]
data = json.loads(raw)
ids = [f.get("id") for f in data.get("files", [])]
if file_id not in ids:
    raise SystemExit(f"file_id {file_id} 不在列表中: {ids}")
PY
green "  file listed"

step "GET /api/v1/files/{id} (download)"
code="$(
  curl -sS --max-time "$CURL_MAX_TIME" -X GET "${API}/files/${FILE_ID}" \
    -H "Authorization: Bearer ${TOKEN}" \
    -o "$DOWNLOAD_FILE" \
    -w '%{http_code}'
)"
[[ "$code" == "200" ]] || die "下载失败 HTTP $code"
[[ "$(cat "$DOWNLOAD_FILE")" == "$EXPECTED_CONTENT" ]] || die "下载内容不匹配"
green "  owner download ok"

step "POST /api/v1/share"
api POST "${API}/share" \
  -H "Authorization: Bearer ${TOKEN}" \
  -H "Content-Type: application/json" \
  -d "{\"file_id\":\"${FILE_ID}\",\"expires_in_hours\":24,\"max_downloads\":10}"
expect_code 200
SHARE_TOKEN="$(json_get "$HTTP_BODY" "share_token")"
[[ -n "$SHARE_TOKEN" ]] || die "share 未返回 share_token: $HTTP_BODY"
green "  share created token=${SHARE_TOKEN}"

step "GET /api/v1/shares"
api GET "${API}/shares" \
  -H "Authorization: Bearer ${TOKEN}"
expect_code 200
python3 - "$SHARE_TOKEN" "$HTTP_BODY" <<'PY'
import json, sys
token, raw = sys.argv[1], sys.argv[2]
items = json.loads(raw)
if not isinstance(items, list):
    raise SystemExit("shares 响应不是数组")
tokens = [item.get("share_token") for item in items]
if token not in tokens:
    raise SystemExit(f"share_token {token} 不在分享列表: {tokens}")
PY
green "  share listed"

step "GET /s/{token} (public download)"
code="$(
  curl -sS --max-time "$CURL_MAX_TIME" -X GET "${BASE_URL}/s/${SHARE_TOKEN}" \
    -H "X-Share-Download: 1" \
    -H "Accept: application/octet-stream" \
    -o "$SHARE_DL_FILE" \
    -w '%{http_code}'
)"
[[ "$code" == "200" ]] || die "分享下载失败 HTTP $code"
[[ "$(cat "$SHARE_DL_FILE")" == "$EXPECTED_CONTENT" ]] || die "分享下载内容不匹配"
green "  share download ok"

step "DELETE /api/v1/share/{token}"
api DELETE "${API}/share/${SHARE_TOKEN}" \
  -H "Authorization: Bearer ${TOKEN}"
expect_code 200
[[ "$(json_get "$HTTP_BODY" "revoked")" == "true" ]] || die "revoke 响应异常: $HTTP_BODY"
green "  share revoked"

step "DELETE /api/v1/files/{id}"
api DELETE "${API}/files/${FILE_ID}" \
  -H "Authorization: Bearer ${TOKEN}"
expect_code 200
[[ "$(json_get "$HTTP_BODY" "deleted")" == "true" ]] || die "delete 响应异常: $HTTP_BODY"
green "  file deleted"

printf '\n'
green "ALL SMOKE TESTS PASSED (${STEP} steps)"
printf '  user=%s  file=%s  share=%s\n' "$USERNAME" "$FILE_ID" "$SHARE_TOKEN"
