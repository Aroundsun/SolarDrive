#!/usr/bin/env bash
# =============================================================================
# SolarDrive 一键 Demo：Docker 启动 → 等待就绪 → API 冒烟测试
#
# 用法：
#   ./scripts/demo.sh
#   BASE_URL=http://127.0.0.1:8080 ./scripts/demo.sh   # 仅跑冒烟（服务已启动时）
# =============================================================================

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BASE_URL="${BASE_URL:-http://127.0.0.1:8080}"
SKIP_COMPOSE="${SKIP_COMPOSE:-0}"
HEALTH_URL="${BASE_URL}/api/v1/health"
WAIT_SECS="${WAIT_SECS:-120}"

green() { printf '\033[32m%s\033[0m\n' "$*"; }
red()   { printf '\033[31m%s\033[0m\n' "$*" >&2; }

if [[ "$SKIP_COMPOSE" != "1" ]]; then
  command -v docker >/dev/null 2>&1 || {
    red "缺少 docker，请先安装 Docker 或使用 SKIP_COMPOSE=1 跳过启动"
    exit 1
  }

  green ">>> docker compose up -d --build"
  docker compose --env-file .env up -d --build

  green ">>> 等待服务就绪 (${HEALTH_URL})"
  deadline=$((SECONDS + WAIT_SECS))
  while (( SECONDS < deadline )); do
    if curl -sf --max-time 3 "$HEALTH_URL" >/dev/null 2>&1; then
      break
    fi
    sleep 2
  done

  if ! curl -sf --max-time 3 "$HEALTH_URL" >/dev/null 2>&1; then
    red "服务在 ${WAIT_SECS}s 内未就绪，请检查: docker compose logs solardrive"
    exit 1
  fi
  green ">>> 服务已就绪"
fi

green ">>> 运行 API 冒烟测试"
BASE_URL="$BASE_URL" exec "$ROOT/scripts/smoke_test.sh"
