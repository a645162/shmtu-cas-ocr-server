---
title: FAQ
---

# 常见问题

## 部署相关

### Q: CPU 版本和 Vulkan GPU 版本有什么区别？

CPU 版本仅使用 CPU 进行推理，适合没有 GPU 或不需要高性能的场景。Vulkan GPU 版本利用 GPU 加速推理，显著降低延迟和提高吞吐量。GPU 模式下，Worker 数量通常只需 1-2 个即可获得优秀性能。

### Q: 如何选择 CPU 还是 GPU 版本？

- 如果服务器有独立 GPU 且支持 Vulkan，推荐使用 GPU 版本
- 如果是云服务器（通常没有 GPU），使用 CPU 版本即可
- CPU 版本在合理的 Worker 配置下也能满足大多数使用场景
- Intel 集成显卡（如 UHD 630）也支持 Vulkan，可以使用 GPU 版本

### Q: Docker 启动后模型加载失败怎么办？

检查以下几点：

1. 确认 `models/` 目录是否存在且有模型文件
2. 如果使用自动下载，检查网络是否可以访问 Gitee 或 GitHub
3. 尝试切换下载源：`SHMTU_MODEL_SOURCE=github` 或 `SHMTU_MODEL_SOURCE=gitee`
4. 查看容器日志：`docker compose logs`
5. 尝试手动下载模型到 `./models` 目录并挂载
6. 确认模型精度与 `SHMTU_PRECISION` 配置匹配（默认 `fp16`）

### Q: GPU 版本报错 "no Vulkan devices found"？

服务会自动检测 Vulkan 设备，如果未找到会降级到 CPU 模式。常见原因：

1. 宿主机未安装 Vulkan 驱动
2. Docker 容器未映射 GPU 设备（需添加 `--device /dev/dri:/dev/dri`）
3. GPU 不支持 Vulkan API
4. 当前构建不包含 Vulkan 支持（未启用 `USE_VULKAN` 编译选项）

验证宿主机 Vulkan 支持：

```bash
vulkaninfo | head -20
```

### Q: 如何选择 Docker 镜像仓库？

- **国内服务器**：推荐使用阿里云 ACR（`registry.cn-shanghai.aliyuncs.com/a645162/shmtu-cas-ocr-server`），拉取速度快
- **国际服务器**：Docker Hub（`a645162/shmtu-cas-ocr-server`）或 GHCR
- **GitHub CI/CD 环境**：GHCR（`ghcr.io/a645162/shmtu-cas-ocr-server`），无需额外认证

### Q: Bundled 镜像和普通镜像有什么区别？

Bundled 镜像将模型权重直接打包在镜像中，启动时无需下载模型。适合以下场景：

- 离线环境或网络受限的服务器
- 需要快速启动的容器编排场景
- 不希望运行时依赖外部下载

缺点是镜像体积更大。如果网络正常，推荐使用普通镜像 + 自动下载。

## 配置相关

### Q: Workers、NCNN Threads、Queue Capacity 应该怎么配置？

建议使用默认的自动调节模式（设为 `0`），系统会根据硬件核心数自动选择最优配置。如果需要手动配置：

- **GPU 模式**：Workers=1-2，NCNN Threads=1，Queue=16-32
- **CPU 模式**：Workers=硬件核心数/4，NCNN Threads=2-4，Queue=Workers*4

自动调节规则详见 [配置参数](/guide/config#自动调节规则)。

### Q: 环境变量和命令行参数哪个优先级更高？

优先级从低到高：默认值 < 环境变量 < 命令行参数。命令行参数会覆盖环境变量和默认值。

### Q: 如何查看当前运行的实际配置？

服务启动时会在日志中打印完整配置信息，也可以通过 `/api/status` 接口查看运行状态。

```bash
# 查看容器启动日志
docker compose logs | head -50

# 通过 API 查看状态
curl http://localhost:21600/api/status
```

### Q: fp16 和 fp32 精度该如何选择？

- **GPU 模式**：推荐 `fp16`，推理速度更快，显存占用更少
- **CPU 模式**：推荐 `fp32`，兼容性更好
- Docker 环境默认使用 `fp16`，如果使用 CPU 版本建议改为 `fp32`

### Q: 如何禁用自动下载模型？

设置 `SHMTU_AUTO_DOWNLOAD_MODELS=0`，容器启动时如果模型文件缺失会报错退出。适合使用只读模型挂载的场景。

## 构建相关

### Q: vcpkg 构建很慢怎么办？

vcpkg 首次构建需要编译所有依赖，后续构建会利用缓存。可以尝试：

1. 使用系统包 preset 代替 vcpkg preset
2. 预先安装 vcpkg 并启用二进制缓存
3. 使用 Docker 构建方式
4. 设置 `CMAKE_BUILD_PARALLEL_LEVEL` 控制并行度

### Q: 构建 GUI 失败，提示找不到 Qt6？

如果当前环境找不到 Qt6 Widgets，CMake 会自动跳过 `shmtu-cas-ocr-gui` 的构建，不会导致整个工程失败。如果需要构建 GUI：

```bash
# 安装 Qt6 开发包
sudo apt install qt6-base-dev

# 使用 GUI preset
cmake --preset linux-system-vulkan-gui
cmake --build --preset build-linux-system-vulkan-gui
```

### Q: NCNN_ROOT 和 vcpkg preset 不能同时使用？

是的。如果显式设置了 `SHMTU_NCNN_ROOT` 或 `NCNN_ROOT` 环境变量，顶层 CMake 会报错并要求改用 `linux-system*` preset。这是因为手动指定 NCNN 路径与 vcpkg 管理的 NCNN 会产生冲突。

### Q: CMake Preset 该怎么选？

根据依赖管理方式和是否需要 Vulkan/GPU 支持：

| 场景 | 推荐 preset |
|------|-------------|
| 有 vcpkg，需要 GPU | `linux-vcpkg-vulkan` |
| 有 vcpkg，不需要 GPU | `linux-vcpkg` |
| 有系统包，需要 GPU | `linux-system-vulkan` |
| 有系统包，不需要 GPU | `linux-system` |
| 统一 CI 流程 | `./scripts/ci_build_system_vulkan.sh` |

详见 [快速开始](/guide/get-started#cmake-presets-一览)。

### Q: 如何控制构建并行度？

构建脚本本身没有写死并行数。当前 `cmake --build` 使用 Ninja 生成器，会按机器核心自动并行。如果需要手动限制：

```bash
export CMAKE_BUILD_PARALLEL_LEVEL=4
cmake --build --preset build-linux-vcpkg-vulkan
```

## API 相关

### Q: /api/ocr 返回 503 怎么办？

503 表示服务过载，排队请求已满。建议：

1. 增加 Worker 数量（`SHMTU_WORKERS`）
2. 增大队列容量（`SHMTU_QUEUE_CAPACITY`）
3. 客户端实现重试逻辑，等待短暂时间后重试

### Q: 识别结果 success=false 是什么意思？

`success=false` 表示 OCR 推理执行了但未能成功识别验证码，可能原因：

1. 图片质量太差或不是有效的验证码图片
2. 图片格式不支持
3. 模型不匹配当前验证码类型
4. 模型精度与加载的模型文件不一致

### Q: Base64 和文件上传两种方式有什么区别？

功能完全相同，只是传参方式不同：

- `/api/ocr` -- JSON Body 传入 Base64 编码图片，适合程序调用
- `/api/ocr/upload` -- multipart 表单上传图片文件，适合直接上传文件

两种方式的响应格式和识别结果完全一致。

### Q: HTTP 和 TCP 服务有什么区别？

- **HTTP**（端口 21600）：RESTful API，支持 JSON 和文件上传，适合 Web 服务和 HTTP 客户端调用
- **TCP**（端口 21601）：简单二进制协议，使用 `<END>` 标记分隔帧，适合需要低延迟或长连接的场景

详见 [API 接口](/guide/api#tcp-服务)。

### Q: operator 字段的数值是什么含义？

`operator` 字段表示运算符类型，支持中文和标准两种样式：

| 值 | 含义 |
|----|------|
| 0 | 加号 `+` |
| 1 | 中文加号 |
| 2 | 减号 `-` |
| 3 | 中文减号 |
| 4 | 乘号 `*` |
| 5 | 中文乘号 |

`equalSymbol` 字段：`0` = 中文等号，`1` = 标准等号 `=`。

## 监控相关

### Q: 如何监控系统运行状态？

使用 `/api/status` 接口获取详细统计数据，包括总请求数、成功/失败数、排队数、活跃 Worker 数等。可以配合 Prometheus 或其他监控工具定期采集。

```bash
# 定期采集状态
watch -n 5 'curl -s http://localhost:21600/api/status | python3 -m json.tool'
```

### Q: availabilityLevel 有哪几个级别？

| 级别 | 条件 |
|------|------|
| `available` | 模型已加载且排队请求数 <= 队列容量/2 |
| `busy` | 排队请求数 > 队列容量/2 |
| `unavailable` | 模型未加载 |

## 其他

### Q: 这个项目可以用于商业用途吗？

不可以。本项目仅供学习交流使用，不得用于商业用途。详见项目免责声明。

### Q: 验证码类型变了，模型需要重新训练吗？

是的。如果验证码样式发生了变化，需要重新收集数据、标注并训练模型。训练代码在 [shmtu-cas-ocr-model](https://github.com/a645162/shmtu-cas-ocr-model) 仓库中。

### Q: 版本号的含义是什么？

当前仓库采用单一版本源 `VERSION` 文件，所有模块（lib、server、cli、gui）使用同一版本号。`2.x` 版本表示服务端已进入 HTTP + TCP 双协议阶段，和只提供 TCP 服务的 `1.x` 版本区分开。

### Q: Docker 镜像的标签规则是什么？

- CPU 主标签：`<version>`、`<major>.<minor>`、`<major>`、`latest`
- CPU 兼容别名：`<version>-cpu`、`latest-cpu`
- GPU/Vulkan 标签：`<version>-vulkan`、`<major>.<minor>-vulkan`、`<major>-vulkan`、`latest-vulkan`
- Bundled 标签：在上述标签后追加 `-bundled`
