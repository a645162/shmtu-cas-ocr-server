---
title: 快速开始
---

# 快速开始

本文档帮助你快速搭建和运行 SHMTU CAS OCR Server。

## 项目简介

SHMTU CAS OCR Server 是上海海事大学统一认证平台验证码识别服务器，基于 C++ 开发，使用 Drogon 作为 HTTP/TCP 框架，NCNN 作为推理引擎。项目采用多目标结构：

- **shmtu-cas-ocr-lib** -- 纯 OCR 推理核心库
- **shmtu-cas-ocr-server** -- Drogon + Trantor 服务端，提供 RESTful API 和 TCP 服务
- **shmtu-cas-ocr-cli** -- 命令行工具，支持本地和远程识别
- **shmtu-cas-ocr-gui** -- Qt6 Widgets 桌面 GUI

其中 `server`、`cli`、`gui` 都依赖 `lib`，而不是把 Web 或 GUI 框架反向耦合到 OCR 核心库里。

## 版本语义

当前仓库采用单一版本源 `VERSION` 文件，`lib`、`server`、`cli`、`gui` 跟随同一发布版本线。当前 `2.x` 表示服务端已经进入 HTTP + TCP 双协议阶段，和只提供 TCP 的 `1.x` 区分开。

## 前置条件

- Ubuntu 22.04+ 或其他 Linux 发行版
- CMake 3.20+
- vcpkg（推荐）或系统包管理器安装依赖
- Vulkan SDK（仅 GPU 加速模式需要）
- Docker（如果使用 Docker 部署方式）

## 获取模型权重

模型权重需要单独下载，请前往 [shmtu-cas-ocr-model](https://github.com/a645162/shmtu-cas-ocr-model) 项目的 [GitHub Release](https://github.com/a645162/shmtu-cas-ocr-model/releases) 下载 NCNN 版权重。

Ubuntu 下可以使用项目内置脚本自动下载 NCNN 预编译包：

```bash
# 默认下载最新版 Ubuntu 24.04 预编译包
python3 3rdparty/NCNN/download_ncnn.py

# 交互式选择系统版本
python3 3rdparty/NCNN/download_ncnn.py --interactive

# 指定版本下载
python3 3rdparty/NCNN/download_ncnn.py --tag 20260526 --ubuntu 2404
```

默认会下载 GitHub Release 最新版的 Ubuntu 24.04 预编译包并解压到 `3rdparty/NCNN/`。当前工程在 Linux 下会自动优先探测该目录内最新的 `ncnn-*-ubuntu-*` 目录，因此下载并解压后一般不需要额外设置路径。

NCNN 搜索路径优先级：

1. `SHMTU_NCNN_ROOT` 环境变量
2. `NCNN_ROOT` 环境变量
3. `3rdparty/NCNN/` 目录下的最新 `ncnn-*-ubuntu-*` 目录

::: warning
如果显式设置了 `SHMTU_NCNN_ROOT` 或 `NCNN_ROOT`，请不要使用 `linux-vcpkg*` preset，CMake 会报错并要求改用 `linux-system*` preset。
:::

## 从源码构建

### 使用 vcpkg（推荐）

```bash
export VCPKG_ROOT=/path/to/vcpkg

# Vulkan GPU 版本
cmake --preset linux-vcpkg-vulkan
cmake --build --preset build-linux-vcpkg-vulkan

# CPU 版本
cmake --preset linux-vcpkg
cmake --build --preset build-linux-vcpkg
```

### 使用系统包

如果 Ubuntu apt 已经安装好依赖，可以直接使用系统包 preset：

```bash
python3 3rdparty/NCNN/download_ncnn.py

# Vulkan GPU 版本
cmake --preset linux-system-vulkan
cmake --build --preset build-linux-system-vulkan
```

说明：

- 这条路径不使用 vcpkg toolchain
- `CLI11`、`glog`、`OpenCV`、`Drogon`、`Trantor`、`Qt6`、`CURL`、`OpenSSL` 会优先由系统包提供
- `libdrogon-dev` 在 Ubuntu 上还需要 `default-libmysqlclient-dev` 和 `libhiredis-dev`
- 推荐使用 Tencent GitHub Release 的 Ubuntu 24.04 NCNN 预编译包

### 构建 GUI

```bash
# 使用 vcpkg
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset linux-vcpkg-vulkan-gui
cmake --build --preset build-linux-vcpkg-vulkan-gui

# 使用系统包
cmake --preset linux-system-vulkan-gui
cmake --build --preset build-linux-system-vulkan-gui
```

如果当前环境找不到 Qt6 Widgets，顶层 CMakeLists.txt 会自动跳过 `shmtu-cas-ocr-gui`，不会导致整个工程配置失败。

### 构建 GUI 所需的 Qt6 安装

```bash
# 安装 Qt6 开发包
sudo apt install qt6-base-dev
```

### Docker 化 system 构建

如果你想统一本地和 CI 的构建流程：

```bash
# CI 路径（不启用 chsrc）
./scripts/ci_build_system_vulkan.sh

# 本地路径（启用 chsrc 换源，适合国内 apt 源较慢的环境）
./scripts/setup_local_system_vulkan.sh
```

它会执行：

1. 下载最新 Ubuntu 24.04 NCNN 预编译包
2. 构建 `Dockerfile.builder-system` builder 镜像
3. 在 builder 容器内编译 `server` / `cli`
4. 将产物复制回工作区 `build/linux-system-vulkan/` 和 `docker-runtime/`
5. 使用 `Dockerfile.runtime-system` 构建 runtime 镜像

### CMake Presets 一览

| Preset | 依赖方式 | Vulkan | GUI |
|--------|----------|--------|-----|
| `linux-vcpkg` | vcpkg | 否 | 否 |
| `linux-vcpkg-vulkan` | vcpkg | 是 | 否 |
| `linux-vcpkg-vulkan-gui` | vcpkg | 是 | 是 |
| `linux-system` | 系统包 | 否 | 否 |
| `linux-system-vulkan` | 系统包 | 是 | 否 |
| `linux-system-vulkan-gui` | 系统包 | 是 | 是 |

构建 preset 命名规则为 `build-` + 对应的 configure preset 名称。

## 使用运行脚本

`scripts/` 目录提供了常用运行脚本：

```bash
# 启动服务端
python3 scripts/run_server.py

# 使用 CLI 工具
python3 scripts/run_cli.py

# 启动 GUI
python3 scripts/run_gui.py

# 检查 lib 推理结果
python3 scripts/run_lib_check.py
```

## 使用 Docker 快速启动

最简单的方式是使用 Docker 部署：

```bash
# 拉取 Vulkan GPU 版本镜像
docker pull a645162/shmtu-cas-ocr-server:latest-vulkan

# 或 CPU 版本
docker pull a645162/shmtu-cas-ocr-server:latest

# 使用 docker-compose 启动
docker compose up -d
```

详细的 Docker 部署说明请参考 [Docker 部署](/guide/docker-deploy)。

## 验证服务

服务启动后，默认监听以下端口：

- HTTP 端口：21600
- TCP 端口：21601

检查服务健康状态：

```bash
curl http://localhost:21600/api/health
```

正常返回示例：

```json
{
  "status": "healthy",
  "modelsLoaded": true,
  "poolSize": 2,
  "serverName": ""
}
```

## CLI 工具

构建完成后，可以使用 CLI 工具进行本地或远程识别测试。CLI 支持 v1 / v2 模型切换：

```bash
# 本地识别（默认 v2）
./shmtu_cas_ocr_cli --model-dir ./models captcha.png

# 显式使用 v1
./shmtu_cas_ocr_cli --model-dir ./models --model-version v1 captcha.png

# 远程识别（连接到服务端）
./shmtu_cas_ocr_cli --server 127.0.0.1:21600 captcha.png
```

详见 [模型管理](/guide/model-management)。

## 依赖栈

当前 `vcpkg.json` 中的主要依赖为：

- `cli11` -- 命令行解析
- `opencv4` -- 图像处理
- `ncnn` -- 推理引擎
- `drogon` -- HTTP 框架
- `trantor` -- TCP 框架

Vulkan 支持通过 manifest feature 启用：`ncnn[vulkan]`

详细的系统依赖说明请参考：

- [Ubuntu vcpkg 系统依赖文档](/ubuntu-vcpkg-system-dependencies)
- [Docker Vulkan 验证流程](/docker-vulkan-host-runtime)

## 下一步

- [Docker 部署](/guide/docker-deploy) -- 详细的 Docker 部署指南
- [API 接口](/guide/api) -- HTTP/TCP 接口文档
- [配置参数](/guide/config) -- 完整的配置参数说明
- [模型管理](/guide/model-management) -- 模型权重获取和管理
