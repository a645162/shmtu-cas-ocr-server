# Scripts

这个目录放的是各个子项目的运行脚本，方便直接启动 `server`、`cli`、`gui`，以及单独检查 `lib`。

## 脚本列表

### `download_models.py`

下载项目默认使用的 NCNN 权重到 `${PROJECT_ROOT}/models`。

示例：

```bash
python3 ./scripts/download_models.py
python3 ./scripts/download_models.py --force
python3 ./scripts/download_models.py --dest /tmp/shmtu-models
```

### `run_server.py`

启动 `shmtu_cas_ocr_server`。

默认参数：

- `--model-dir ${PROJECT_ROOT}/models`
- `--http-port 21600`
- `--tcp-port 21601`
- `--precision fp16`
- 默认启用 `--use-gpu`

示例：

```bash
python3 ./scripts/run_server.py
python3 ./scripts/run_server.py --workers 4 --queue-capacity 32
SHMTU_HTTP_PORT=3000 SHMTU_TCP_PORT=3001 python3 ./scripts/run_server.py
SHMTU_USE_GPU=0 python3 ./scripts/run_server.py
SHMTU_WORKERS=0 SHMTU_NCNN_THREADS=0 SHMTU_QUEUE_CAPACITY=0 python3 ./scripts/run_server.py
```

### `run_cli.py`

启动 `shmtu_cas_ocr_cli`。

默认参数：

- `--model-dir ${PROJECT_ROOT}/models`
- `--precision fp16`
- 默认启用 `--use-gpu`

示例：

```bash
python3 ./scripts/run_cli.py captcha.png
python3 ./scripts/run_cli.py --json ./captcha_images
SHMTU_USE_GPU=0 python3 ./scripts/run_cli.py ./captcha_images
```

### `run_gui.py`

启动 `shmtu_cas_ocr_gui`。

默认参数：

- `--model-dir ${PROJECT_ROOT}/models`
- `--precision fp16`
- 当 `SHMTU_USE_GPU=1` 时附加 `--use-gpu`

示例：

```bash
python3 ./scripts/run_gui.py
SHMTU_MODEL_DIR=/opt/shmtu-models python3 ./scripts/run_gui.py
SHMTU_PRECISION=fp32 SHMTU_USE_GPU=1 python3 ./scripts/run_gui.py
```

### `run_lib_check.py`

`shmtu-cas-ocr-lib` 是库目标，不是独立程序。这个脚本用于单独构建检查该库。

示例：

```bash
python3 ./scripts/run_lib_check.py
SHMTU_BUILD_PRESET=build-linux-vcpkg python3 ./scripts/run_lib_check.py
```

## 二进制查找规则

运行脚本会按下面顺序寻找可执行文件：

1. `SHMTU_BUILD_DIR`
2. GUI: `${PROJECT_ROOT}/build/linux-vcpkg-vulkan-gui` 或 `${PROJECT_ROOT}/build/linux-vcpkg-gui`
3. 非 GUI: `${PROJECT_ROOT}/build/linux-vcpkg-vulkan`
4. `${PROJECT_ROOT}/build/linux-vcpkg`

如果没有找到对应二进制，脚本会直接报错并退出。

## 环境变量

### 通用

- `SHMTU_BUILD_DIR`
  - 显式指定构建目录。
- `SHMTU_MODEL_DIR`
  - 指定模型目录。
- `SHMTU_PRECISION`
  - 指定模型精度，例如 `fp16`、`fp32`。
- `SHMTU_USE_GPU`
  - `1` 表示启用 GPU，`0` 表示禁用 GPU。

### 仅 `run_server.py`

- `SHMTU_HTTP_PORT`
  - HTTP 监听端口。
- `SHMTU_TCP_PORT`
  - TCP 监听端口。
- `SHMTU_WORKERS`
  - OCR worker 数量。`0` 表示按 CPU 核心和运行模式自动调节。
- `SHMTU_QUEUE_CAPACITY`
  - 最大排队请求数。`0` 表示按 worker 数自动调节。
- `SHMTU_NCNN_THREADS`
  - 每个 worker 的 NCNN CPU 线程数。`0` 表示自动调节。
- `SHMTU_HTTP_HOST`
  - HTTP 监听地址。
- `SHMTU_TCP_HOST`
  - TCP 监听地址。
- `SHMTU_SERVER_NAME`
  - 在 `/api/health` 和 `/api/status` 中附加 `serverName`。

### 仅 `run_lib_check.py`

- `SHMTU_BUILD_PRESET`
  - 默认值为 `build-linux-vcpkg-vulkan`。

## 建议

如果你准备长期在本仓库内开发，建议先把工程配置到：

```bash
cmake --preset linux-vcpkg-vulkan
cmake --build --preset build-linux-vcpkg-vulkan
```

如果要运行 GUI，建议用：

```bash
cmake --preset linux-vcpkg-vulkan-gui
cmake --build --preset build-linux-vcpkg-vulkan-gui
```

这样所有目标都会统一落在 `build/` 下的子目录中，不再单独使用 `build-gui/` 或 `/tmp` 构建目录。
