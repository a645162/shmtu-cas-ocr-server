---
title: Docker部署
---

# Docker 部署

Docker 是部署 SHMTU CAS OCR Server 最简单的方式。项目提供了多阶段 Dockerfile，支持 CPU 和 Vulkan GPU 两种运行时目标。

## 镜像版本

项目发布以下 Docker 镜像标签：

### 标准镜像

| 标签模式 | 说明 |
|----------|------|
| `latest` / `latest-cpu` | CPU 最新版 |
| `<version>` / `<version>-cpu` | CPU 指定版本（如 `2.0.0`、`2.0.0-cpu`） |
| `<major>.<minor>` / `<major>.<minor>-cpu` | CPU 兼容别名（如 `2.0`、`2.0-cpu`） |
| `<major>` / `<major>-cpu` | CPU 大版本别名（如 `2`、`2-cpu`） |
| `latest-vulkan` | Vulkan GPU 最新版 |
| `<version>-vulkan` | Vulkan GPU 指定版本（如 `2.0.0-vulkan`） |
| `<major>.<minor>-vulkan` | Vulkan GPU 兼容别名 |
| `<major>-vulkan` | Vulkan GPU 大版本别名 |

### Bundled 镜像（内置模型权重）

| 标签模式 | 说明 |
|----------|------|
| `latest-bundled` | CPU 内置模型最新版 |
| `<version>-bundled` | CPU 内置模型指定版本 |
| `latest-vulkan-bundled` | Vulkan GPU 内置模型最新版 |
| `<version>-vulkan-bundled` | Vulkan GPU 内置模型指定版本 |

Bundled 镜像将模型权重直接打包在镜像中，适合离线环境部署，但镜像体积会更大。

例如完整标签：`a645162/shmtu-cas-ocr-server:2.0.0-vulkan`

## 镜像仓库

Docker 镜像同时发布到以下三个仓库：

| 仓库 | 镜像地址 | 适用场景 |
|------|----------|----------|
| Docker Hub | `a645162/shmtu-cas-ocr-server` | 国际网络，默认选择 |
| GHCR | `ghcr.io/a645162/shmtu-cas-ocr-server` | GitHub 生态，CI/CD 集成 |
| 阿里云 ACR | `registry.cn-shanghai.aliyuncs.com/a645162/shmtu-cas-ocr-server` | 国内网络，下载速度快 |

国内用户推荐使用阿里云 ACR 镜像，拉取速度远快于 Docker Hub。

## 快速启动

### 使用 docker-compose（推荐）

项目根目录已提供 `docker-compose.yml`，配合 `.env` 文件即可一键启动：

```bash
# 使用默认配置启动
docker compose up -d
```

`docker-compose.yml` 完整配置：

```yaml
services:
  shmtu-cas-ocr-server:
    image: ${SHMTU_RUNTIME_IMAGE:-a645162/shmtu-cas-ocr-server:latest-vulkan}
    container_name: shmtu-cas-ocr-server
    restart: unless-stopped
    ports:
      - "21600:21600"
      - "21601:21601"
    volumes:
      - ./models:/app/models
      - ./logs:/app/logs
    environment:
      SHMTU_MODEL_DIR: /app/models
      SHMTU_LOG_DIR: /app/logs
      SHMTU_LOG_FILE: ${SHMTU_LOG_FILE:-/app/logs/shmtu-cas-ocr-server.log}
      SHMTU_HTTP_PORT: 21600
      SHMTU_TCP_PORT: 21601
      SHMTU_USE_GPU: ${SHMTU_USE_GPU:-1}
      SHMTU_WORKERS: ${SHMTU_WORKERS:-0}
      SHMTU_QUEUE_CAPACITY: ${SHMTU_QUEUE_CAPACITY:-0}
      SHMTU_NCNN_THREADS: ${SHMTU_NCNN_THREADS:-0}
      SHMTU_SERVER_NAME: ${SHMTU_SERVER_NAME:-}
      SHMTU_MODEL_SOURCE: ${SHMTU_MODEL_SOURCE:-gitee}
      OCR_MODEL_VERSION: ${OCR_MODEL_VERSION:-v2}
    healthcheck:
      test: ["CMD", "curl", "-fsS", "http://127.0.0.1:21600/api/health"]
      interval: 30s
      timeout: 5s
      retries: 3
      start_period: 10s
    # GPU runtime:
    # 1. 保持 SHMTU_USE_GPU=1
    # 2. 如果宿主使用 Mesa/Intel/AMD Vulkan，取消下面 devices 注释
    # devices:
    #   - /dev/dri:/dev/dri
```

如果需要自定义配置，建议新建 `.env.local` 文件：

```bash
docker compose --env-file .env.local up -d
```

### 手动 Docker 运行

```bash
# CPU 版本
docker run -d \
  --name shmtu-ocr-server \
  -p 21600:21600 \
  -p 21601:21601 \
  -v ./models:/app/models \
  -v ./logs:/app/logs \
  a645162/shmtu-cas-ocr-server:latest

# Vulkan GPU 版本
docker run -d \
  --name shmtu-ocr-server \
  -p 21600:21600 \
  -p 21601:21601 \
  -v ./models:/app/models \
  -v ./logs:/app/logs \
  --device /dev/dri:/dev/dri \
  a645162/shmtu-cas-ocr-server:latest-vulkan
```

## GPU 支持

Vulkan GPU 加速需要宿主机支持 Vulkan 运行时：

### Intel/AMD 集成显卡（Mesa Vulkan）

在 `docker-compose.yml` 中取消 `devices` 注释：

```yaml
devices:
  - /dev/dri:/dev/dri
```

或手动运行时添加 `--device /dev/dri:/dev/dri`。

### NVIDIA 独立显卡

NVIDIA GPU 用户需安装 [NVIDIA Container Toolkit](https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/latest/install-guide.html)，安装后 Docker 会自动映射 GPU 设备。

### 验证宿主机 Vulkan 支持

```bash
vulkaninfo | head -20
```

如果输出中能看到 GPU 设备信息，说明 Vulkan 驱动已正确安装。如果服务端启用了 GPU 但未检测到 Vulkan 设备，会自动降级到 CPU 模式并输出 WARNING 日志。

## 容器启动流程

容器启动时会执行以下流程：

1. **初始化日志** -- 创建日志目录和文件，将 stdout/stderr 重定向到日志文件
2. **模型引导** -- 检查 `/app/models` 目录中是否存在所需模型文件
3. **自动下载** -- 如果模型文件缺失，从配置的源（GitHub 或 Gitee）下载
4. **启动服务** -- 执行 OCR Server 主进程

### 模型下载源配置

通过 `SHMTU_MODEL_SOURCE` 环境变量控制下载源：

- `gitee`（默认）-- 国内用户推荐，下载速度快
- `github` -- 国际网络环境使用

当主源下载失败时，会自动尝试备用源（GitHub 和 Gitee 互为备用）。每个文件最多重试 3 次。下载完成后会验证 SHA256 校验和。

## 模型权重挂载

### 自动下载模式（默认）

容器启动时会自动下载模型权重到 `/app/models` 目录，无需手动操作。`docker-compose.yml` 已默认配置 `./models:/app/models` 卷映射，下载的模型会持久化到宿主机。

### 挂载本地模型

如果已经提前下载了模型，可以挂载本地目录避免每次启动时下载：

```bash
# 下载模型到本地
mkdir -p ./models
# 将 NCNN 模型文件放入 ./models/

# 挂载本地模型目录
docker run -d \
  -v ./models:/app/models \
  ... \
  a645162/shmtu-cas-ocr-server:latest-vulkan
```

### 禁用自动下载

如果希望容器不自动下载模型（例如使用只读挂载）：

```bash
docker run -d \
  -e SHMTU_AUTO_DOWNLOAD_MODELS=0 \
  -v ./models:/app/models:ro \
  ... \
  a645162/shmtu-cas-ocr-server:latest-vulkan
```

当 `SHMTU_AUTO_DOWNLOAD_MODELS=0` 时，如果模型文件缺失，容器会报错退出。

### 使用 Bundled 镜像

使用内置模型的镜像无需挂载模型目录，**默认捆绑 v2 fp16 mobilenet_v3_small 单模型**：

```bash
docker run -d \
  --name shmtu-ocr-server \
  -p 21600:21600 \
  -p 21601:21601 \
  a645162/shmtu-cas-ocr-server:latest-vulkan-bundled
```

> bundled 镜像目前仅打包 v2。如需 v1 bundled 镜像，请自行修改 `Dockerfile` 多阶段 COPY 段，或改用普通镜像 + 挂载本地 v1 权重目录。

## 环境变量配置

通过环境变量控制运行时行为，所有并发参数默认 `0 = 自动调节`：

| 环境变量 | 默认值 | 说明 |
|----------|--------|------|
| `SHMTU_RUNTIME_IMAGE` | `a645162/shmtu-cas-ocr-server:latest-vulkan` | 运行时镜像（docker-compose 使用） |
| `SHMTU_USE_GPU` | `1` | 是否启用 GPU |
| `SHMTU_WORKERS` | `0` | OCR Worker 数量（0=自动） |
| `SHMTU_NCNN_THREADS` | `0` | 每个 Worker 的 NCNN CPU 线程数（0=自动） |
| `SHMTU_QUEUE_CAPACITY` | `0` | 最大排队请求数（0=自动） |
| `SHMTU_MODEL_DIR` | `/app/models` | 模型文件目录 |
| `SHMTU_MODEL_SOURCE` | `gitee` | 模型下载源（`gitee` 或 `github`） |
| `SHMTU_PRECISION` | `fp16` | 模型精度（`fp16` 或 `fp32`，v1 生效） |
| `SHMTU_AUTO_DOWNLOAD_MODELS` | `1` | 是否自动下载缺失模型（`0` 或 `1`） |
| `OCR_MODEL_VERSION` | `v2` | 模型版本（`v1` 或 `v2`） |
| `SHMTU_LOG_DIR` | `/app/logs` | 日志目录 |
| `SHMTU_LOG_FILE` | `/app/logs/shmtu-cas-ocr-server.log` | 日志文件路径 |
| `SHMTU_SERVER_NAME` | (空) | 服务名称，显示在 /api/health |
| `SHMTU_HTTP_PORT` | `21600` | HTTP 端口 |
| `SHMTU_TCP_PORT` | `21601` | TCP 端口 |

示例 -- 固定版本并调节并发：

```bash
SHMTU_RUNTIME_IMAGE=a645162/shmtu-cas-ocr-server:2.0.0-vulkan \
SHMTU_WORKERS=4 \
SHMTU_NCNN_THREADS=2 \
SHMTU_QUEUE_CAPACITY=32 \
docker compose up -d
```

## .env 文件

项目根目录提供了 `.env` 文件作为 docker-compose 的默认配置：

```bash
# 运行时镜像
SHMTU_RUNTIME_IMAGE=a645162/shmtu-cas-ocr-server:latest-vulkan

# 运行时调优
SHMTU_USE_GPU=1
SHMTU_WORKERS=0
SHMTU_QUEUE_CAPACITY=0
SHMTU_NCNN_THREADS=0

# 模型引导源
SHMTU_MODEL_SOURCE=gitee

# 模型版本（v1 = 3 模型 ResNet；v2 = 单模型 MobileNetV3 Tri-Slot Decoder，默认）
OCR_MODEL_VERSION=v2

# 容器日志路径
SHMTU_LOG_FILE=/app/logs/shmtu-cas-ocr-server.log

# 服务名称
SHMTU_SERVER_NAME=
```

::: tip
建议不要直接修改 `.env` 文件。如需自定义配置，新建 `.env.local` 文件并使用 `docker compose --env-file .env.local up -d`。
:::

## 健康检查

`docker-compose.yml` 已内置健康检查，每 30 秒探测一次 `/api/health`：

```yaml
healthcheck:
  test: ["CMD", "curl", "-fsS", "http://127.0.0.1:21600/api/health"]
  interval: 30s
  timeout: 5s
  retries: 3
  start_period: 10s
```

手动检查：

```bash
curl http://localhost:21600/api/health
```

查看容器日志：

```bash
docker compose logs -f
```

## 自行构建镜像

### 使用多阶段 Dockerfile（vcpkg 构建）

```bash
# CPU 版本
docker build -f Dockerfile --target runtime-cpu -t shmtu-ocr-server:cpu .

# Vulkan GPU 版本
docker build -f Dockerfile --target runtime-gpu -t shmtu-ocr-server:vulkan .

# 内置模型的 CPU 版本
docker build -f Dockerfile --target runtime-cpu-bundled -t shmtu-ocr-server:cpu-bundled .

# 内置模型的 GPU 版本
docker build -f Dockerfile --target runtime-gpu-bundled -t shmtu-ocr-server:vulkan-bundled .
```

Dockerfile 构建阶段说明：

| 阶段 | 说明 |
|------|------|
| `builder-base` | 基础构建环境（Ubuntu 24.04 + vcpkg） |
| `builder-cpu` | CPU 版本编译 |
| `builder-gpu` | Vulkan GPU 版本编译 |
| `runtime-cpu` | CPU 运行时镜像 |
| `runtime-gpu` | Vulkan GPU 运行时镜像（含 Vulkan 和 Mesa 驱动） |
| `runtime-cpu-bundled` | CPU 运行时 + 内置模型 |
| `runtime-gpu-bundled` | Vulkan GPU 运行时 + 内置模型 |

### 使用 system 依赖构建链路

如果使用 system 依赖构建（不使用 vcpkg）：

```bash
./scripts/ci_build_system_vulkan.sh
```

该脚本会：

1. 下载最新 Ubuntu 24.04 NCNN 预编译包
2. 构建 `Dockerfile.builder-system` builder 镜像
3. 在 builder 容器内编译 server 和 cli
4. 将产物复制到 `build/linux-system-vulkan/` 和 `docker-runtime/`
5. 使用 `Dockerfile.runtime-system` 构建 runtime 镜像

本地 Ubuntu 如果 apt 源较慢，可以使用：

```bash
./scripts/setup_local_system_vulkan.sh
```

它和 CI 走同一套 Docker 构建脚本，只是在 builder 镜像内额外执行 `chsrc set ubuntu` 换源。

## CI/CD 发布流程

项目通过 GitHub Actions（`.github/workflows/build-system-vulkan.yml`）自动发布镜像。当推送 tag 时触发：

1. **构建** -- CPU 和 Vulkan 两个变体并行构建
2. **推送 GHCR** -- 先推送到 GitHub Container Registry
3. **推送 Docker Hub** -- 从 GHCR 复制到 Docker Hub
4. **推送阿里云 ACR** -- 从 GHCR 复制到阿里云 ACR（`cn-shanghai` 区域）
5. **发布 GitHub Release** -- 附带二进制和 SHA256 校验文件

所有三个仓库的镜像标签保持一致。
