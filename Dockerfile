# =============================================================
# SolarDrive — 多阶段构建
# =============================================================

# ---- 阶段一：编译 ----
FROM docker.m.daocloud.io/library/ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN sed -i 's|archive.ubuntu.com|mirrors.aliyun.com|g' /etc/apt/sources.list && \
    sed -i 's|security.ubuntu.com|mirrors.aliyun.com|g' /etc/apt/sources.list

RUN apt update && apt install -y \
    build-essential cmake pkg-config \
    libssl-dev \
    libpq-dev libpqxx-dev \
    libhiredis-dev \
    libyaml-cpp-dev \
    nlohmann-json3-dev \
    libspdlog-dev \
    libfmt-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

RUN cmake -S . -B build && cmake --build build -j"$(nproc)"

# ---- 阶段二：运行 ----
FROM docker.m.daocloud.io/library/ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN sed -i 's|archive.ubuntu.com|mirrors.aliyun.com|g' /etc/apt/sources.list && \
    sed -i 's|security.ubuntu.com|mirrors.aliyun.com|g' /etc/apt/sources.list

RUN apt update && apt install -y \
    libssl3 \
    libpq5 libpqxx-6.4 \
    libhiredis0.14 \
    libyaml-cpp0.7 \
    libspdlog1 \
    libfmt8 \
    postgresql-client \
    redis-tools \
    curl \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /app/build/solardrive ./build/solardrive
COPY --from=builder /app/config ./config
COPY --from=builder /app/web ./web
COPY deploy/entrypoint.sh ./entrypoint.sh

RUN chmod +x ./entrypoint.sh && \
    mkdir -p /tmp/solardrive/objects logs

EXPOSE 8080

HEALTHCHECK --interval=10s --timeout=5s --retries=5 --start-period=20s \
    CMD curl -fsS http://127.0.0.1:8080/api/v1/health || exit 1

ENTRYPOINT ["./entrypoint.sh"]
