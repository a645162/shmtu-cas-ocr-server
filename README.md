# 上海海事大学 统一认证平台 验证码识别服务器

ShangHai Maritime University CAS OCR Server

[![Publish System Docker Images](https://github.com/a645162/shmtu-cas-ocr-server/actions/workflows/build-system-vulkan.yml/badge.svg)](https://github.com/a645162/shmtu-cas-ocr-server/actions/workflows/build-system-vulkan.yml)

## 模型权重

请前往[shmtu-cas-ocr-model](https://github.com/a645162/shmtu-cas-ocr-model)项目的[Github Release](https://github.com/a645162/shmtu-cas-ocr-model/releases)中下载NCNN版权重。

Ubuntu 下如果使用 Tencent 官方预编译版 ncnn，可运行：

```bash
python3 3rdparty/NCNN/download_ncnn.py
```

默认会下载 GitHub Release 最新版的 `Ubuntu 24.04` 预编译包，并解压到 `3rdparty/NCNN/`。如果需要交互式选择系统版本，可使用 `--interactive`。如果需要指定版本，可使用：

```bash
python3 3rdparty/NCNN/download_ncnn.py --tag 20260526 --ubuntu 2404
```

当前工程在 Linux 下会自动优先探测 `3rdparty/NCNN/` 内最新的 `ncnn-*-ubuntu-*` 目录，因此下载并解压后一般不需要额外设置路径。

## 当前结构

当前仓库已经调整为多目标结构：

* `shmtu-cas-ocr-lib`
  * 纯 OCR 推理核心库
* `shmtu-cas-ocr-server`
  * `Drogon + Trantor` 服务端，同时提供 RESTful API 和 TCP 服务
* `shmtu-cas-ocr-cli`
  * 命令行工具
* `shmtu-cas-ocr-gui`
  * `Qt6 Widgets` 桌面 GUI

`server`、`cli`、`gui` 都依赖 `lib`，而不是把 Web 或 GUI 框架反向耦合到 OCR 核心库里。

## Build

当前仓库已切换为 `vcpkg manifest` 依赖管理，不再使用 Conan。

### 依赖栈

当前 `vcpkg.json` 中的主要依赖为：

* `cli11`
* `opencv4`
* `ncnn`
* `drogon`
* `trantor`

Vulkan 支持通过 manifest feature 启用：

* `ncnn[vulkan]`

### Ubuntu 系统依赖

部分 Linux 桌面和构建工具依赖需要通过 `apt` 安装，请先阅读：

* [Ubuntu vcpkg 系统依赖文档](Documents/docs/ubuntu-vcpkg-system-dependencies.md)
* [Docker Vulkan 验证流程](Documents/docs/docker-vulkan-host-runtime.md)

### Linux 构建

推荐使用 `CMakePresets.json`：

```bash
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset linux-vcpkg-vulkan
cmake --build --preset build-linux-vcpkg-vulkan
```

如果 Ubuntu `apt` 已经安装好了依赖，并且你不想用 `vcpkg`，可以直接走系统包 preset：

```bash
python3 3rdparty/NCNN/download_ncnn.py
cmake --preset linux-system-vulkan
cmake --build --preset build-linux-system-vulkan
```

如果你想统一本地和 CI 的流程，仓库也提供了 Docker 化 system 构建链路：

```bash
./scripts/ci_build_system_vulkan.sh
```

它会执行：

* 下载最新 Ubuntu 24.04 `ncnn` 预编译包
* 构建 `Dockerfile.builder-system`
* 在 builder 容器内编译 `server` / `cli`
* 将产物复制回工作区 `build/linux-system-vulkan/` 和 `docker-runtime/`
* 再用 [Dockerfile.runtime-system](/home/konghaomin/Prj/SHMTU/shmtu-terminal/Server/shmtu-cas-ocr-server/Dockerfile.runtime-system:1) 构建 runtime 镜像

说明：

* `./scripts/ci_build_system_vulkan.sh` 不启用 `chsrc`，GitHub Actions 也走这条默认路径。

如果你在本地 Ubuntu 上遇到 `apt` 源较慢，使用本地 wrapper 即可。它和 CI 走同一套 Docker 构建脚本，只是在 builder 镜像内额外执行 `chsrc set ubuntu`：

```bash
./scripts/setup_local_system_vulkan.sh
```

如果你要构建 GUI：

```bash
cmake --preset linux-system-vulkan-gui
cmake --build --preset build-linux-system-vulkan-gui
```

说明：

* 这条路径不会使用 `vcpkg` toolchain
* `CLI11`、`glog`、`OpenCV`、`Drogon`、`Trantor`、`Qt6`、`CURL`、`OpenSSL` 会优先由系统包提供
* `libdrogon-dev` 在 Ubuntu 上还需要 `default-libmysqlclient-dev` 和 `libhiredis-dev`
* `ncnn` 推荐使用 Tencent GitHub Release 的 Ubuntu 24.04 预编译包
* 在 Linux 下当前工程会优先尝试 `SHMTU_NCNN_ROOT`、`NCNN_ROOT`、`3rdparty/NCNN/` 下的本地包
* 如果显式设置了 `SHMTU_NCNN_ROOT` 或 `NCNN_ROOT`，请不要再使用 `linux-vcpkg*` preset；顶层 CMake 会直接报错并要求改用 `linux-system*`
* Docker 化 system 构建的默认 builder 镜像名为 `shmtu-cas-ocr-builder:system-vulkan`，runtime 本地镜像名为 `shmtu-cas-ocr-server:vulkan`
* CI 发布的远端镜像 tag 采用：
  * CPU 主标签：`<version>`、`<major>.<minor>`、`<major>`、`latest`
  * CPU 兼容别名：`<version>-cpu`、`latest-cpu`
  * GPU/Vulkan 标签：`<version>-vulkan`、`<major>.<minor>-vulkan`、`<major>-vulkan`、`latest-vulkan`
* 当前仓库内的 `docker-compose.yml` 默认直接消费 `a645162/shmtu-cas-ocr-server:latest-vulkan`

runtime 镜像构建完成后可以直接启动：

```bash
docker compose up -d
```

当前仓库已提供可提交的默认配置文件 `.env`。如果你想写自己机器上的覆盖配置，建议新建 `.env.local`，并显式指定：

```bash
docker compose --env-file .env.local up -d
```

并发参数通过环境变量控制，默认都支持 `0 = 自动调节`：

```bash
SHMTU_USE_GPU=1 \
SHMTU_WORKERS=0 \
SHMTU_NCNN_THREADS=0 \
SHMTU_QUEUE_CAPACITY=0 \
docker compose up -d
```

例如固定到某个服务版本：

```bash
SHMTU_RUNTIME_IMAGE=a645162/shmtu-cas-ocr-server:2.0.0-vulkan docker compose up -d
```

版本设计上，当前仓库采用单一版本源 `VERSION`，并默认让 `lib/server/cli/gui` 跟随同一发布版本线。当前 `2.x` 表示服务端已经进入 HTTP + TCP 双协议阶段，和只提供 TCP 的 `1.x` 区分开。

构建脚本本身没有写死 `-j4`。当前 `cmake --build` 直接交给底层生成器处理；在现有 preset 下默认使用 `Ninja`，会按机器核心自动并行。如果你需要手动限制并发，建议设置 `CMAKE_BUILD_PARALLEL_LEVEL`，而不是把脚本写死成固定线程数。

例如：

```bash
export NCNN_ROOT=/path/to/ncnn-install
cmake --preset linux-system-vulkan
cmake --build --preset build-linux-system-vulkan
```

如果暂时不启用 Vulkan：

```bash
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset linux-vcpkg
cmake --build --preset build-linux-vcpkg
```

如果你要构建 GUI：

```bash
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset linux-vcpkg-vulkan-gui
cmake --build --preset build-linux-vcpkg-vulkan-gui
```

如果当前环境找不到 `Qt6 Widgets`，顶层 `CMakeLists.txt` 会自动跳过 `shmtu-cas-ocr-gui`，不会让整个工程配置失败。

如果你希望手动安装 manifest feature，也可以使用：

```bash
$VCPKG_ROOT/vcpkg install --x-manifest-root=. --feature-flags=manifests --x-no-default-features --x-feature=vulkan
```

## 运行脚本

`scripts/` 目录已经提供了常用运行脚本：

* `scripts/run_server.py`
* `scripts/run_cli.py`
* `scripts/run_gui.py`
* `scripts/run_lib_check.py`

详细说明见：

* [Scripts 使用说明](scripts/README.md)

## 本系列项目

### 客户端

* Go Wails版
  [https://github.com/a645162/SHMTU-Terminal-Wails](https://github.com/a645162/SHMTU-Terminal-Wails)
* Rust Tauri版(画个饼，或许以后会做吧~)

### 服务器部署模型

验证码OCR识别系列项目今后将只会维护推理服务器(shmtu-cas-ocr-server)这一个项目。

[https://github.com/a645162/shmtu-cas-ocr-server](https://github.com/a645162/shmtu-cas-ocr-server)

注：这个项目为王老师的研究生课程《机器视觉》的课程设计项目，仅用作学习用途！！！

### 统一认证登录流程(数字平台+微信平台)

* Kotlin版(方便移植Android)
  [https://github.com/a645162/shmtu-cas-kotlin](https://github.com/a645162/shmtu-cas-kotlin)
* Go版(为Wails桌面客户端做准备)
  [https://github.com/a645162/shmtu-cas-go](https://github.com/a645162/shmtu-cas-go)
* Rust版(未来想做Tauri桌面客户端可能会移植)
  ps.功能其实和Golang版本没啥区别，甚至可能实现地更费劲，Golang的移植已经让我比较抓狂了，虽然Rust我也是会的，但是或许不会做。。。

注：这个项目为王老师的研究生课程《机器视觉》的课程设计项目，仅用作学习用途！！！

### 模型训练

**神经网络图像分类模型训练**

使用PyTorch以及经典网络ResNet

[https://github.com/a645162/shmtu-cas-ocr-model](https://github.com/a645162/shmtu-cas-ocr-model)

**人工标注的数据集(2选1下载)**

* Hugging Face
  https://huggingface.co/datasets/a645162/shmtu_cas_validate_code
* Gitee AI(国内较快)
  https://ai.gitee.com/datasets/a645162/shmtu_cas_validate_code

训练代码中包含爬虫代码，以及自动测试识别结果代码。
您可以对其修改，对测试通过的图片进行标注，这样可以获得准确的标注。

注：这个项目为王老师的研究生课程《机器视觉》的课程设计项目，仅用作学习用途！！！

### 模型本地部署

* Windows客户端(包括VC Win32 GUI以及C# WPF)
  [https://github.com/a645162/shmtu-cas-ocr-demo-windows](https://github.com/a645162/shmtu-cas-ocr-demo-windows)
* Qt客户端(支持Windows/macOS/Linux)
  [https://github.com/a645162/shmtu-cas-ocr-demo-qt](https://github.com/a645162/shmtu-cas-ocr-demo-qt)
* Android客户端
  [https://github.com/a645162/shmtu-cas-demo-android](https://github.com/a645162/shmtu-cas-demo-android)

注：这3个项目为王老师的研究生课程《机器视觉》的课程设计项目，仅用作学习用途！！！

### 原型测试

Python+Selenium4自动化测试数字海大平台登录流程

[https://github.com/a645162/Digital-SHMTU-Tools](https://github.com/a645162/Digital-SHMTU-Tools)

注：本项目为付老师的研究生课程《Python程序设计与开发》的课程设计项目，仅用作学习用途！！！

## 免责声明

本(系列)项目仅供学习交流使用，不得用于商业用途，如有侵权请联系作者删除。

本(系列)项目为个人开发，与上海海事大学无关，仅供学习参考，请勿用于非法用途。

本(系列)项目为孔昊旻同学的**课程设计**项目，仅用作学习用途！！！
