---
title: 配置参数
---

# 配置参数

SHMTU CAS OCR Server 支持三种配置方式，按优先级从低到高为：

1. **默认值** -- 内置默认配置
2. **环境变量** -- 通过 `SHMTU_*` 环境变量覆盖
3. **命令行参数** -- 通过 CLI 选项覆盖，优先级最高

## 命令行参数

通过 `--help` 查看完整参数列表：

```bash
./shmtu_cas_ocr_server --help
```

输出包含所有选项、默认值和支持的环境变量列表。

### 网络配置

| 参数 | 环境变量 | 默认值 | 说明 |
|------|----------|--------|------|
| `--http-host` | `SHMTU_HTTP_HOST` | `0.0.0.0` | HTTP 绑定地址 |
| `--http-port` | `SHMTU_HTTP_PORT` | `21600` | HTTP 端口（1-65535） |
| `--tcp-host` | `SHMTU_TCP_HOST` | `0.0.0.0` | TCP 绑定地址 |
| `--tcp-port` | `SHMTU_TCP_PORT` | `21601` | TCP 端口（1-65535） |

### 模型配置

| 参数 | 环境变量 | 默认值 | 说明 |
|------|----------|--------|------|
| `--model-dir` | `SHMTU_MODEL_DIR` | `./models` | 模型文件目录路径 |
| `--model-version` | `OCR_MODEL_VERSION` | `v2` | 模型版本：`v1` 或 `v2` |
| `--precision` | `SHMTU_PRECISION` | `fp16` | 模型精度：`fp16` 或 `fp32`（v1 生效） |

`--precision` 只接受 `fp16` 和 `fp32` 两个值，其他值会导致启动失败。
`--model-version` 接受 `v1` / `v2`，未知值回退到 `v2`。

HTTP 请求体内的 `version` 字段可在运行时按次覆盖（见 [API 接口](/guide/api)）。

### 并发配置

所有并发参数设为 `0` 时自动根据硬件调节。

| 参数 | 环境变量 | 默认值 | 说明 |
|------|----------|--------|------|
| `--workers` | `SHMTU_WORKERS` | `0` | OCR Worker 数量（0=自动） |
| `--ncnn-threads` | `SHMTU_NCNN_THREADS` | `0` | 每个 Worker 的 NCNN CPU 线程数（0=自动） |
| `--queue-capacity` | `SHMTU_QUEUE_CAPACITY` | `0` | 最大排队请求数（0=自动） |

### 自动调节规则

当参数设为 `0`（自动）时，系统按以下规则调节：

- **NCNN 线程数**：
  - GPU 模式 -> 1 线程
  - CPU 模式 -> min(硬件核心数, 4) 线程

- **Worker 数量**：
  - GPU 模式 -> min(硬件核心数, 2) 个 Worker
  - CPU 模式 -> min(硬件核心数 / NCNN线程数, 8) 个 Worker

- **队列容量**：max(16, Worker数 * 4)

示例 -- 8 核 CPU 服务器自动调节结果：

| 参数 | CPU 模式 | GPU 模式 |
|------|----------|----------|
| NCNN 线程数 | 4 | 1 |
| Worker 数量 | 2 | 2 |
| 队列容量 | 16 | 16 |

### GPU 配置

| 参数 | 环境变量 | 默认值 | 说明 |
|------|----------|--------|------|
| `--use-gpu` / `--no-use-gpu` | `SHMTU_USE_GPU` | `false` | 启用/禁用 GPU 加速 |

环境变量 `SHMTU_USE_GPU` 接受以下值：

- 启用：`1`、`true`、`on`、`yes`（不区分大小写）
- 禁用：`0`、`false`、`off`、`no`（不区分大小写）

如果启用了 GPU 但未检测到 Vulkan 设备，服务会自动降级到 CPU 模式并输出 WARNING 日志。如果当前构建不包含 Vulkan 支持（未启用 `USE_VULKAN`），也会输出 WARNING 并以 CPU 模式运行。

`--use-gpu` 和 `--no-use-gpu` 不能同时指定，否则启动失败。

### 日志配置

| 参数 | 环境变量 | 默认值 | 说明 |
|------|----------|--------|------|
| `--log-dir` | `SHMTU_LOG_DIR` | `./logs` | 日志目录 |
| `--log-file-prefix` | `SHMTU_LOG_FILE_PREFIX` | `shmtu_cas_ocr_server` | 日志文件前缀 |
| `--log-min-level` | `SHMTU_LOG_MIN_LEVEL` | `0` | glog 最小日志级别（0=INFO, 1=WARNING, 2=ERROR, 3=FATAL） |
| `--log-to-stderr` / `--no-log-to-stderr` | `SHMTU_LOG_TO_STDERR` | `false` | 日志直接输出到 stderr |
| `--also-log-to-stderr` / `--no-also-log-to-stderr` | `SHMTU_LOG_ALSO_TO_STDERR` | `true` | 同时将日志镜像输出到 stderr |
| `--log-max-size-mb` | `SHMTU_LOG_MAX_SIZE_MB` | `10` | 日志文件最大大小（MB） |
| `--log-cleanup-interval-secs` | `SHMTU_LOG_CLEANUP_INTERVAL_SECS` | `3600` | 日志清理间隔（秒，0=禁用定期清理） |
| `--log-retention-days` | `SHMTU_LOG_RETENTION_DAYS` | `7` | 日志保留天数（0=禁用清理删除） |

说明：

- `--log-to-stderr` 将日志直接输出到 stderr，不写入文件
- `--also-log-to-stderr` 在写入文件的同时镜像输出到 stderr（Docker 环境下默认开启，方便 `docker logs` 查看）
- `--log-max-size-mb` 达到上限后 glog 会自动轮转日志文件
- `--log-cleanup-interval-secs` 和 `--log-retention-days` 配合使用，定期清理超过保留天数的日志文件
- `--log-to-stderr` 和 `--no-log-to-stderr` 不能同时指定
- `--also-log-to-stderr` 和 `--no-also-log-to-stderr` 不能同时指定

### 其他配置

| 参数 | 环境变量 | 默认值 | 说明 |
|------|----------|--------|------|
| `--server-name` | `SHMTU_SERVER_NAME` | (空) | 服务逻辑名称，显示在 /api/health 和 /api/status |

`server-name` 用于在多实例部署中区分不同节点，例如 `ocr-node-1`、`ocr-node-2`。

## Docker Compose 环境变量

在 `docker-compose.yml` 中使用的环境变量：

```yaml
environment:
  SHMTU_MODEL_DIR: /app/models
  SHMTU_LOG_DIR: /app/logs
  SHMTU_LOG_FILE: /app/logs/shmtu-cas-ocr-server.log
  SHMTU_HTTP_PORT: 21600
  SHMTU_TCP_PORT: 21601
  SHMTU_USE_GPU: 1
  SHMTU_WORKERS: 0
  SHMTU_QUEUE_CAPACITY: 0
  SHMTU_NCNN_THREADS: 0
  SHMTU_SERVER_NAME: ""
  SHMTU_MODEL_SOURCE: gitee
```

## Docker 运行时环境变量

除了上述服务配置变量外，Docker 运行时还支持以下变量：

| 环境变量 | 默认值 | 说明 |
|----------|--------|------|
| `SHMTU_MODEL_SOURCE` | `gitee` | 模型下载源（`gitee` 或 `github`） |
| `SHMTU_PRECISION` | `fp16` | 模型精度（`fp16` 或 `fp32`，v1 生效） |
| `SHMTU_AUTO_DOWNLOAD_MODELS` | `1` | 是否自动下载缺失模型（`0` 或 `1`） |
| `SHMTU_MODEL_BASE_URL` | GitHub Release URL | 模型下载主 URL |
| `SHMTU_MODEL_FALLBACK_BASE_URL` | Gitee Release URL | 模型下载备用 URL |
| `OCR_MODEL_VERSION` | `v2` | 模型版本（`v1` 或 `v2`） |

## 配置示例

### 高并发 CPU 模式

```bash
./shmtu_cas_ocr_server \
  --http-port 21600 \
  --tcp-port 21601 \
  --workers 8 \
  --ncnn-threads 4 \
  --queue-capacity 64 \
  --model-dir ./models
```

适合无 GPU 的多核服务器，Worker 数量和线程数根据核心数合理分配。

### GPU 加速模式

```bash
./shmtu_cas_ocr_server \
  --use-gpu \
  --workers 2 \
  --ncnn-threads 1 \
  --queue-capacity 16 \
  --model-dir ./models
```

GPU 模式下 Worker 数量通常只需 1-2 个，NCNN 线程数设为 1 即可获得优秀性能。

### 最小化配置（自动调节）

```bash
./shmtu_cas_ocr_server \
  --model-dir ./models \
  --use-gpu
```

所有并发参数使用自动调节，系统根据硬件核心数自动选择最优配置。

### Docker 环境自定义

```bash
# 在 .env.local 中配置
SHMTU_RUNTIME_IMAGE=a645162/shmtu-cas-ocr-server:2.0.0-vulkan
SHMTU_USE_GPU=1
SHMTU_WORKERS=4
SHMTU_NCNN_THREADS=2
SHMTU_QUEUE_CAPACITY=32
SHMTU_MODEL_SOURCE=gitee
SHMTU_SERVER_NAME=ocr-prod-1

# 使用自定义配置启动
docker compose --env-file .env.local up -d
```

## 启动时配置输出

服务启动时会在日志中打印完整配置信息，便于确认实际生效的配置：

```
Configuration:
  HTTP host:      0.0.0.0
  HTTP port:      21600
  TCP host:       0.0.0.0
  TCP port:       21601
  Model dir:      ./models
  Model version:  v2
  Precision:      fp16
  Workers:        2
  NCNN threads:   1
  Use GPU:        true
  Queue capacity: 16
  Log dir:        ./logs
  Log prefix:     shmtu_cas_ocr_server
  Log level:      0
```

如果 GPU 模式下成功检测到 Vulkan 设备，还会输出设备信息：

```
GPU Devices (1):
  [0] Intel(R) UHD Graphics 630 (CML GT2) (API 4202671, 64 MB)
```

也可以通过 `/api/status` 接口在运行时查看当前状态。
