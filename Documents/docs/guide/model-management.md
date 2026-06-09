---
title: 模型管理
---

# 模型管理

SHMTU CAS OCR Server 同时支持 v1（legacy）与 v2（**默认**）两套模型。本章介绍两套模型的获取、部署、切换与管理。

## 模型版本对比

| 维度 | v1 (legacy) | v2 (**默认**) |
|------|-------------|---------------|
| 模型数量 | 3 个独立模型 | **1 个** |
| 一次推理 | 4 次前向（先判等号样式 → 算子 → 两数字） | **1 次** 端到端 |
| Backbone | resnet18 / resnet34 | `mobilenet_v3_small` |
| 输入 | RGB 3×224×224 | 灰度 1×64×192 |
| 精度 | fp16 / fp32 | fp16 |
| 运算符类别 | 6 类（含中文） | 3 类（`+`、`-`、`×`） |
| Release Tag | `v1.0-NCNN` | `v2.0.x` |
| 资产清单 | `SHA256SUMS.txt` | `model-assets.json`（含 hash 字段） |

## 模型权重来源

模型权重托管在 [shmtu-cas-ocr-model](https://github.com/a645162/shmtu-cas-ocr-model) 项目中：

- **v1 权重**：从 release 标签 `v1.0-NCNN` 下载
- **v2 权重**：从 release 标签 `v2.0.x` 下载，配套 `model-assets.json` 清单

### 模型训练

验证码识别模型使用 PyTorch 训练，训练代码同样在 shmtu-cas-ocr-model 仓库中。训练流程包括：

1. 爬取验证码图片数据
2. 人工标注或半自动标注
3. 训练分类模型
4. 导出为 NCNN 格式

**训练数据集：**

- [Hugging Face](https://huggingface.co/datasets/a645162/shmtu_cas_validate_code)
- [Gitee AI](https://ai.gitee.com/datasets/a645162/shmtu_cas_validate_code)（国内较快）

训练代码中包含爬虫代码以及自动测试识别结果代码，你可以对其修改，对测试通过的图片进行标注，这样可以获得准确的标注。

## 模型版本切换

| 入口 | 说明 |
|------|------|
| `--model-version v1\|v2` | CLI 参数（默认 `v2`） |
| `OCR_MODEL_VERSION` | 环境变量（默认 `v2`） |
| HTTP 请求体 `version` 字段 | 运行时按次覆盖（`/api/ocr`、`/api/ocr/upload`） |

示例：

```bash
# 启动时指定 v2（默认）
./shmtu_cas_ocr_server --model-version v2

# 启动时指定 v1
./shmtu_cas_ocr_server --model-version v1

# 启动后单次请求覆盖
curl -X POST http://localhost:21600/api/ocr \
  -H 'Content-Type: application/json' \
  -d '{"imageBase64":"...","version":"v1"}'
```

## 模型精度

精度参数只对 v1 生效；v2 当前仅提供 fp16。

| 精度 | 说明 | 适用场景 |
|------|------|----------|
| `fp16` | 半精度浮点，默认值 | GPU 模式下推理更快，显存占用更少 |
| `fp32` | 单精度浮点 | 兼容性最好，CPU 模式推荐使用 |

```bash
./shmtu_cas_ocr_server --model-version v1 --precision fp32
```

## v2 模型目录结构（默认）

v2 仅需 1 个模型（NCNN 三件套）：

```
models/
├── mobilenet_v3_small.trislot_decoder.v2_0.fp16.param
├── mobilenet_v3_small.trislot_decoder.v2_0.fp16.bin
```

> 部分 backbone 还会附带 `.bin` 之外的元数据文件，请以 `model-assets.json` 清单为准。

## v1 模型目录结构

v1 沿用旧的 6 文件结构：

**fp16 精度（默认）：**

```
models/
├── resnet18_equal_symbol_latest.fp16.param
├── resnet18_equal_symbol_latest.fp16.bin
├── resnet18_operator_latest.fp16.param
├── resnet18_operator_latest.fp16.bin
├── resnet34_digit_latest.fp16.param
└── resnet34_digit_latest.fp16.bin
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

1. 读取启动配置中的 `OCR_MODEL_VERSION`（默认 `v2`）
2. 检查 `MODEL_DIR` 中是否存在对应版本的全部必需文件
3. 如果所有文件已存在，跳过下载
4. 如果有缺失文件，从配置的源下载
5. 主源下载失败时自动尝试备用源（GitHub 和 Gitee 互为备用）
6. 每个文件最多重试 3 次
7. **v1**：从 `SHA256SUMS.txt` 验证；**v2**：从 `model-assets.json` 的 hash 字段验证

### v1 校验

v1 仍使用 `SHA256SUMS.txt` 校验文件完整性。如果校验失败，会删除损坏的文件并重试下载。

### v2 校验（`model-assets.json`）

v2 的资产清单文件位于 release 根目录，形如：

```json
{
  "tag": "v2.0.2",
  "assets": [
    {
      "name": "mobilenet_v3_small.trislot_decoder.v2_0.fp16.param",
      "url": "...",
      "sha256": "..."
    },
    {
      "name": "mobilenet_v3_small.trislot_decoder.v2_0.fp16.bin",
      "url": "...",
      "sha256": "..."
    }
  ]
}
```

下载脚本按 `{tag, backbone, precision, engine}` 维度在 manifest 中查找匹配资产，对应文件用清单内嵌的 `sha256` 字段校验。

### 相关环境变量

| 环境变量 | 默认值 | 说明 |
|----------|--------|------|
| `SHMTU_MODEL_DIR` | `/app/models` | 模型文件目录 |
| `SHMTU_MODEL_SOURCE` | `gitee` | 下载源：`gitee` 或 `github` |
| `SHMTU_PRECISION` | `fp16` | 模型精度（v1 生效） |
| `SHMTU_AUTO_DOWNLOAD_MODELS` | `1` | 是否自动下载缺失模型 |
| `SHMTU_MODEL_BASE_URL` | GitHub Release URL | 主下载 URL |
| `SHMTU_MODEL_FALLBACK_BASE_URL` | Gitee Release URL | 备用下载 URL |
| `OCR_MODEL_VERSION` | `v2` | 模型版本（`v1` 或 `v2`） |

### 禁用自动下载

```bash
docker run -d \
  -e SHMTU_AUTO_DOWNLOAD_MODELS=0 \
  -v ./models:/app/models:ro \
  a645162/shmtu-cas-ocr-server:latest-vulkan
```

当 `SHMTU_AUTO_DOWNLOAD_MODELS=0` 时，如果模型文件缺失，容器会输出错误信息并退出。

## Docker 环境下的模型管理

### 默认镜像（v2）

`runtime-cpu` / `runtime-gpu` 默认目标**捆绑 v2 fp16 mobilenet_v3_small 单模型权重**。v2 仅 1 个模型（`.param` + `.bin`），相比 v1 的 6 文件结构，镜像体积更小。

### 内置模型镜像（bundled）

bundled 镜像目前仅打包 v2；如需 v1 bundled 镜像请自行修改 Dockerfile 多阶段 COPY 段。

```bash
# v2 bundled 镜像
docker pull a645162/shmtu-cas-ocr-server:latest-vulkan-bundled
docker build -f Dockerfile --target runtime-gpu-bundled -t shmtu-ocr-server:vulkan-bundled .
```

### 在 Docker 中使用 v1

```bash
# 1. 准备 v1 模型目录
mkdir -p ./models
# 把 v1 6 个权重文件放入 ./models/

# 2. 启动时显式指定 v1
docker run -d \
  -e OCR_MODEL_VERSION=v1 \
  -e SHMTU_AUTO_DOWNLOAD_MODELS=0 \
  -v ./models:/app/models \
  a645162/shmtu-cas-ocr-server:latest-vulkan
```

或在 `docker-compose.yml` 中：

```yaml
environment:
  OCR_MODEL_VERSION: v1
  SHMTU_AUTO_DOWNLOAD_MODELS: 0
volumes:
  - ./models:/app/models
```

## NCNN 预编译包

在 Linux 下从源码构建时，项目会优先探测 `3rdparty/NCNN/` 目录下的 NCNN 预编译包：

```bash
python3 3rdparty/NCNN/download_ncnn.py
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

更新模型时只需替换 `models/` 目录中的文件，然后重启服务：

```bash
docker compose restart
```

服务启动后会自动加载新模型，无需额外操作。

如果验证码样式发生了变化，需要重新收集数据、标注并训练模型。训练代码在 [shmtu-cas-ocr-model](https://github.com/a645162/shmtu-cas-ocr-model) 仓库中。

## 使用下载脚本

```bash
# 默认下载 v2（推荐）
python3 scripts/download_models.py

# 下载 v1
python3 scripts/download_models.py --version v1
```

该脚本会在 Docker 构建和 CI 流程中使用，也可在本地运行。
