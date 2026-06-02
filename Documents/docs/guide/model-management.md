---
title: 模型管理
---

# 模型管理

SHMTU CAS OCR Server 使用 NCNN 格式的模型权重进行验证码识别。本章介绍模型的获取、部署和管理方式。

## 模型权重来源

模型权重托管在 [shmtu-cas-ocr-model](https://github.com/a645162/shmtu-cas-ocr-model) 项目中，需要从 [GitHub Release](https://github.com/a645162/shmtu-cas-ocr-model/releases) 下载 NCNN 版权重。

### 模型训练

验证码识别模型使用 PyTorch 和经典网络 ResNet 训练，训练代码同样在 shmtu-cas-ocr-model 仓库中。训练流程包括：

1. 爬取验证码图片数据
2. 人工标注或半自动标注
3. 使用 ResNet 训练分类模型
4. 导出为 NCNN 格式

**训练数据集：**

- [Hugging Face](https://huggingface.co/datasets/a645162/shmtu_cas_validate_code)
- [Gitee AI](https://ai.gitee.com/datasets/a645162/shmtu_cas_validate_code)（国内较快）

训练代码中包含爬虫代码以及自动测试识别结果代码，你可以对其修改，对测试通过的图片进行标注，这样可以获得准确的标注。

## 模型精度

Server 支持两种模型精度，通过 `--precision` 参数或 `SHMTU_PRECISION` 环境变量选择：

| 精度 | 说明 | 适用场景 |
|------|------|----------|
| `fp16` | 半精度浮点，默认值 | GPU 模式下推理更快，显存占用更少 |
| `fp32` | 单精度浮点 | 兼容性最好，CPU 模式推荐使用 |

```bash
# 使用 fp16 精度（默认）
./shmtu_cas_ocr_server --precision fp16 --use-gpu

# 使用 fp32 精度
./shmtu_cas_ocr_server --precision fp32

# 或通过环境变量
SHMTU_PRECISION=fp16 ./shmtu_cas_ocr_server --use-gpu
```

::: tip
Docker 环境下默认使用 `fp16` 精度。如果使用 CPU 版本，建议切换为 `fp32` 以获得更好的兼容性。
:::

## 模型目录结构

模型文件应放置在 `--model-dir` 指定的目录中，默认为 `./models`。

### 所需模型文件

根据精度不同，需要的模型文件如下：

**fp16 精度（默认）：**

```
models/
├── resnet18_equal_symbol_latest.fp16.param    # 等号分类模型结构
├── resnet18_equal_symbol_latest.fp16.bin      # 等号分类模型权重
├── resnet18_operator_latest.fp16.param        # 运算符分类模型结构
├── resnet18_operator_latest.fp16.bin          # 运算符分类模型权重
├── resnet34_digit_latest.fp16.param           # 数字分类模型结构
└── resnet34_digit_latest.fp16.bin             # 数字分类模型权重
```

**fp32 精度：**

```
models/
├── resnet18_equal_symbol_latest.fp32.param
├── resnet18_equal_symbol_latest.fp32.bin
├── resnet18_operator_latest.fp32.param
├── resnet18_operator_latest.fp32.bin
├── resnet34_digit_latest.fp32.param
└── resnet34_digit_latest.fp32.bin
```

每个精度版本包含 3 个模型（6 个文件），分别用于：

| 模型 | 网络 | 用途 |
|------|------|------|
| `resnet18_equal_symbol` | ResNet-18 | 识别等号样式（中文等号 / 标准等号） |
| `resnet18_operator` | ResNet-18 | 识别运算符（加 / 减 / 乘） |
| `resnet34_digit` | ResNet-34 | 识别数字（0-9） |

## 自动下载机制

Docker 容器启动时会自动检查模型文件是否存在。如果缺少任何必需文件，会触发自动下载。

### 下载流程

1. 检查 `MODEL_DIR` 中是否存在所有必需的模型文件
2. 如果所有文件已存在，跳过下载
3. 如果有缺失文件，从配置的源下载
4. 主源下载失败时自动尝试备用源（GitHub 和 Gitee 互为备用）
5. 每个文件最多重试 3 次
6. 下载完成后验证 SHA256 校验和

### SHA256 校验和验证

模型下载脚本会自动从 Release 页面获取 `SHA256SUMS.txt` 文件，用于验证下载文件的完整性。如果校验失败，会删除损坏的文件并重试下载。

如果 `SHA256SUMS.txt` 文件本身也无法下载，会跳过校验步骤，仅检查文件是否非空。

### 相关环境变量

| 环境变量 | 默认值 | 说明 |
|----------|--------|------|
| `SHMTU_MODEL_DIR` | `/app/models` | 模型文件目录 |
| `SHMTU_MODEL_SOURCE` | `gitee` | 下载源：`gitee` 或 `github` |
| `SHMTU_PRECISION` | `fp16` | 模型精度 |
| `SHMTU_AUTO_DOWNLOAD_MODELS` | `1` | 是否自动下载缺失模型 |
| `SHMTU_MODEL_BASE_URL` | GitHub Release URL | 主下载 URL |
| `SHMTU_MODEL_FALLBACK_BASE_URL` | Gitee Release URL | 备用下载 URL |

### 禁用自动下载

在某些场景下（如只读挂载或安全要求），可以禁用自动下载：

```bash
# 通过环境变量禁用
docker run -d \
  -e SHMTU_AUTO_DOWNLOAD_MODELS=0 \
  -v ./models:/app/models:ro \
  ... \
  a645162/shmtu-cas-ocr-server:latest-vulkan
```

当 `SHMTU_AUTO_DOWNLOAD_MODELS=0` 时，如果模型文件缺失，容器会输出错误信息并退出，不会启动服务。

## Docker 环境下的模型管理

### 自动下载模式（推荐）

容器启动时会自动从 Gitee 或 GitHub 下载模型权重到 `/app/models` 目录。下载源通过 `SHMTU_MODEL_SOURCE` 控制：

- `gitee`（默认）-- 国内用户推荐，下载速度快
- `github` -- 国际网络环境使用

```bash
# 使用 Gitee 源（默认）
SHMTU_MODEL_SOURCE=gitee docker compose up -d

# 使用 GitHub 源
SHMTU_MODEL_SOURCE=github docker compose up -d
```

由于 `docker-compose.yml` 默认配置了 `./models:/app/models` 卷映射，下载的模型会持久化到宿主机，后续重启无需重复下载。

### 挂载本地模型

如果已经提前下载了模型，可以挂载本地目录避免每次启动时下载：

```bash
# 将模型文件放在宿主机 ./models 目录
docker run -d \
  -v ./models:/app/models \
  ... \
  a645162/shmtu-cas-ocr-server:latest-vulkan
```

在 `docker-compose.yml` 中已默认配置：

```yaml
volumes:
  - ./models:/app/models
```

### 内置模型镜像

项目提供了 bundled 版本的 Docker 镜像，模型权重直接打包在镜像中：

```bash
# 拉取内置模型的 GPU 版本
docker pull a645162/shmtu-cas-ocr-server:latest-vulkan-bundled

# 或构建内置模型镜像
docker build -f Dockerfile --target runtime-gpu-bundled -t shmtu-ocr-server:vulkan-bundled .
```

这种方式适合离线环境部署，但镜像体积会更大。

## NCNN 预编译包

在 Linux 下从源码构建时，项目会优先探测 `3rdparty/NCNN/` 目录下的 NCNN 预编译包。可以使用脚本自动下载：

```bash
# 默认下载最新版 Ubuntu 24.04 预编译包
python3 3rdparty/NCNN/download_ncnn.py

# 交互式选择系统版本
python3 3rdparty/NCNN/download_ncnn.py --interactive

# 指定版本
python3 3rdparty/NCNN/download_ncnn.py --tag 20260526 --ubuntu 2404
```

NCNN 搜索路径优先级：

1. `SHMTU_NCNN_ROOT` 环境变量
2. `NCNN_ROOT` 环境变量
3. `3rdparty/NCNN/` 目录下的最新 `ncnn-*-ubuntu-*` 目录

::: warning
如果显式设置了 `SHMTU_NCNN_ROOT` 或 `NCNN_ROOT`，请不要使用 `linux-vcpkg*` preset，CMake 会报错并要求改用 `linux-system*` preset。
:::

## 模型更新

更新模型时只需替换 `models/` 目录中的文件，然后重启服务即可：

```bash
# Docker 环境
docker compose restart

# 原生环境
# 停止当前服务后重新启动
./shmtu_cas_ocr_server --model-dir ./models --use-gpu
```

服务启动后会自动加载新模型，无需额外操作。

如果验证码样式发生了变化，需要重新收集数据、标注并训练模型。训练代码在 [shmtu-cas-ocr-model](https://github.com/a645162/shmtu-cas-ocr-model) 仓库中。

## 使用下载脚本

项目提供了 Python 模型下载脚本，可用于在宿主机上预先下载模型：

```bash
# 下载模型到默认目录
python3 scripts/download_models.py
```

该脚本会在 Docker 构建和 CI 流程中使用，也可在本地运行。
