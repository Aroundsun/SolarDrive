#!/bin/bash
set -euo pipefail

PGHOST="${PGHOST:-postgres}"
PGPORT="${PGPORT:-5432}"
REDIS_HOST="${REDIS_HOST:-redis}"
CONFIG="${SOLAR_CONFIG:-config/config.yaml}"

echo "[entrypoint] waiting for PostgreSQL at ${PGHOST}:${PGPORT}..."
for i in $(seq 1 60); do
  if pg_isready -h "${PGHOST}" -p "${PGPORT}" -U postgres -d solardrive >/dev/null 2>&1; then
    echo "[entrypoint] PostgreSQL is ready"
    break
  fi
  if [ "${i}" -eq 60 ]; then
    echo "[entrypoint] ERROR: PostgreSQL not ready after 60s"
    exit 1
  fi
  sleep 1
done

echo "[entrypoint] waiting for Redis at ${REDIS_HOST}:6379..."
for i in $(seq 1 30); do
  if redis-cli -h "${REDIS_HOST}" ping 2>/dev/null | grep -q PONG; then
    echo "[entrypoint] Redis is ready"
    break
  fi
  if [ "${i}" -eq 30 ]; then
    echo "[entrypoint] WARN: Redis not ready, continuing anyway"
    break
  fi
  sleep 1
done

if [ ! -f "${CONFIG}" ]; then
  echo "[entrypoint] ERROR: config file not found: ${CONFIG}"
  ls -la config/ 2>/dev/null || true
  exit 1
fi

if [ ! -f web/index.html ]; then
  echo "[entrypoint] ERROR: web UI not found at web/index.html"
  ls -la web/ 2>/dev/null || true
  exit 1
fi

mkdir -p /tmp/solardrive/objects logs

echo "[entrypoint] starting SolarDrive with ${CONFIG}..."
exec ./build/solardrive -c "${CONFIG}"
