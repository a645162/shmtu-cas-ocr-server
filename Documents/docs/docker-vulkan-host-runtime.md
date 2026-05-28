# Docker Vulkan 构建与运行流程

本文档记录当前推荐的两阶段流程：

1. 先用 Docker builder 容器编译 Vulkan 版 `server` / `cli`
2. 将 builder 产物复制回宿主机工作区
3. 再用独立 runtime Docker 镜像封装这些产物
4. 运行容器并直通 Intel 核显 `/dev/dri`
5. 用本地 `cli` 连接容器内服务端进行远程识别测试

这份流程用于快速验证：

* `ncnn[vulkan]` 是否构建成功
* 容器内是否能识别宿主机核显
* 服务端是否能在 Docker 中正常提供 OCR 能力

当前文档对应的是“builder 容器编译 + runtime 容器打包”的方案。它比之前的“直接挂宿主机库目录”更接近 CI 可复用流程。

## 前提条件

宿主机需要满足：

* Linux
* 已安装 Docker
* 模型文件已经放在项目 `models/` 目录中

可先检查：

```bash
docker --version
ls -l /dev/dri
```

## 1. 用 Docker builder 编译 Vulkan 版 server / cli

CI 和本地共用：

```bash
./scripts/ci_build_system_vulkan.sh
```

本地如果需要 `chsrc` 换源，使用：

```bash
./scripts/setup_local_system_vulkan.sh
```

执行完成后，产物会同时存在于：

```text
build/linux-system-vulkan/ocr/shmtu-cas-ocr-server/shmtu_cas_ocr_server
build/linux-system-vulkan/ocr/shmtu-cas-ocr-cli/shmtu_cas_ocr_cli
docker-runtime/shmtu_cas_ocr_server
docker-runtime/shmtu_cas_ocr_cli
```

同时会生成 runtime 镜像：

```text
shmtu-cas-ocr-server:system-vulkan
```

## 2. 启动 runtime 容器并直通核显

示例命令：

```bash
docker run -d \
  --name shmtu-cas-ocr-system-vulkan \
  -p 21620:21600 \
  -p 21621:21601 \
  --device /dev/dri:/dev/dri \
  -v /home/konghaomin/Prj/SHMTU/shmtu-terminal/Server/shmtu-cas-ocr-server/models:/app/models:ro \
  shmtu-cas-ocr-server:system-vulkan
```

说明：

* 容器内服务端监听 `21600` / `21601`
* 映射到宿主机后，对外测试端口为 `21620` / `21621`
* 这里仅需直通 `/dev/dri` 和挂载模型目录

## 3. 检查容器日志

```bash
docker logs shmtu-cas-ocr-system-vulkan
```

如果 Vulkan 和核显都正常，日志中应出现类似内容：

```text
Use GPU:        true
[0 Intel(R) UHD Graphics 630 (CML GT2)]
```

这说明：

* 服务端已按 `--use-gpu` 启动
* 容器中已枚举到 Intel 核显 Vulkan 设备

如果没有 GPU，服务端代码会回退到 CPU，并打印 warning。

## 4. 健康检查

```bash
curl -fsS http://127.0.0.1:21620/api/health
```

示例返回：

```json
{"availabilityLevel":2,"modelsLoaded":true,"pendingRequests":0,"poolSize":2,"queueCapacity":16,"reason":"","status":"healthy"}
```

其中：

* `modelsLoaded: true` 表示模型已成功加载
* `status: healthy` 表示服务端可以处理请求

## 5. 用本地 CLI 测试远程识别

示例命令：

```bash
build/linux-system-vulkan/ocr/shmtu-cas-ocr-cli/shmtu_cas_ocr_cli \
  --server 127.0.0.1:21620 \
  ./tmp/real-captcha-samples/captcha_01.png
```

示例结果：

```text
SHMTU CAS OCR CLI V2.2.0
Remote server: 127.0.0.1:21620
Server health: OK

[./tmp/real-captcha-samples/captcha_01.png] 5 * 6 = 30  =>  30  (remote)
```

说明：

* 本地 `cli` 已成功连通 Docker 内服务端
* OCR 请求已由容器内服务端处理并返回

## 6. 清理

停止并删除测试容器：

```bash
docker rm -f shmtu-cas-ocr-system-vulkan
```

如果也要删除测试镜像：

```bash
docker rmi shmtu-cas-ocr-server:system-vulkan
```

## 关键结论

这次流程验证了以下几点：

* Docker builder 可以稳定产出 Ubuntu 24.04 system+Vulkan 二进制
* builder 产物可以复制回宿主机工作区
* runtime 镜像可以直接从这些产物构建
* 运行时只需 `/dev/dri` 和模型目录即可验证 Intel Vulkan 直通
