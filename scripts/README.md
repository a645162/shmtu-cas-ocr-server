# Scripts

这个目录放的是各个子项目的运行脚本，方便直接启动 `server`、`cli`、`gui`，以及单独检查 `lib`。

## 脚本列表

### `run-server.sh`

启动 `shmtu_cas_ocr_server`。

默认参数：

- `--model-dir ${PROJECT_ROOT}/models`
- `--http-port 21600`
- `--tcp-port 21601`
- `--precision fp16`
- 默认启用 `--use-gpu`

示例：

```bash
./scripts/run-server.sh
./scripts/run-server.sh --workers 4 --queue-capacity 32
SHMTU_HTTP_PORT=3000 SHMTU_TCP_PORT=3001 ./scripts/run-server.sh
SHMTU_USE_GPU=0 ./scripts/run-server.sh
```

### `run-cli.sh`

启动 `shmtu_cas_ocr_cli`。

默认参数：

- `--model-dir ${PROJECT_ROOT}/models`
- `--precision fp16`
- 默认启用 `--use-gpu`

示例：

```bash
./scripts/run-cli.sh captcha.png
./scripts/run-cli.sh --json ./captcha_images
SHMTU_USE_GPU=0 ./scripts/run-cli.sh ./captcha_images
```

### `run-gui.sh`

启动 `shmtu_cas_ocr_gui`。

示例：

```bash
./scripts/run-gui.sh
```

### `run-lib-check.sh`

`shmtu-cas-ocr-lib` 是库目标，不是独立程序。这个脚本用于单独构建检查该库。

示例：

```bash
./scripts/run-lib-check.sh
SHMTU_BUILD_PRESET=build-linux-vcpkg ./scripts/run-lib-check.sh
```

## 二进制查找规则

运行脚本会按下面顺序寻找可执行文件：

1. `SHMTU_BUILD_DIR`
2. `${PROJECT_ROOT}/build/linux-vcpkg-vulkan`
3. `/tmp/shmtu-drogon-vulkan-config`

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

### 仅 `run-server.sh`

- `SHMTU_HTTP_PORT`
  - HTTP 监听端口。
- `SHMTU_TCP_PORT`
  - TCP 监听端口。

### 仅 `run-lib-check.sh`

- `SHMTU_BUILD_PRESET`
  - 默认值为 `build-linux-vcpkg-vulkan`。

## 建议

如果你准备长期在本仓库内开发，建议先把工程配置到：

```bash
cmake --preset linux-vcpkg-vulkan
cmake --build --preset build-linux-vcpkg-vulkan
```

这样脚本会优先从 `build/linux-vcpkg-vulkan` 直接启动，不再依赖 `/tmp/shmtu-drogon-vulkan-config`。
