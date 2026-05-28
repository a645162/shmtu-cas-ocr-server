# Docker Vulkan 验证流程

本文档记录一次已经验证通过的流程：

1. 宿主机构建 Vulkan 版 `server` / `cli`
2. 将宿主机构建产物封装进 runtime Docker 镜像
3. 运行容器并直通 Intel 核显 `/dev/dri`
4. 用本地 `cli` 连接容器内服务端进行远程识别测试

这份流程用于快速验证：

* `ncnn[vulkan]` 是否构建成功
* 容器内是否能识别宿主机核显
* 服务端是否能在 Docker 中正常提供 OCR 能力

注意：本文档对应的是“运行时挂载宿主机 Vulkan/系统库”的验证方案，不是完全自包含的发布镜像方案。

## 前提条件

宿主机需要满足：

* Linux
* `VCPKG_ROOT` 已配置
* 已安装 Docker
* 宿主机存在 `/dev/dri`
* 模型文件已经放在项目 `models/` 目录中

可先检查：

```bash
echo "$VCPKG_ROOT"
docker --version
ls -l /dev/dri
```

## 1. 宿主机构建 Vulkan 版 server / cli

先使用 Vulkan preset 配置并构建：

```bash
export VCPKG_ROOT=/home/konghaomin/vcpkg

cmake --preset linux-vcpkg-vulkan
cmake --build --preset build-linux-vcpkg-vulkan --target shmtu_cas_ocr_server shmtu_cas_ocr_cli
```

成功后会生成：

```text
build/linux-vcpkg-vulkan/ocr/shmtu-cas-ocr-server/shmtu_cas_ocr_server
build/linux-vcpkg-vulkan/ocr/shmtu-cas-ocr-cli/shmtu_cas_ocr_cli
```

## 2. 准备 runtime 打包目录

本仓库提供了 runtime-only Dockerfile：

* [Dockerfile.runtime-host](/home/konghaomin/Prj/SHMTU/shmtu-terminal/Server/shmtu-cas-ocr-server/Dockerfile.runtime-host:1)

将已构建的宿主机二进制复制到 `docker-runtime/`：

```bash
mkdir -p docker-runtime
cp build/linux-vcpkg-vulkan/ocr/shmtu-cas-ocr-server/shmtu_cas_ocr_server docker-runtime/
cp build/linux-vcpkg-vulkan/ocr/shmtu-cas-ocr-cli/shmtu_cas_ocr_cli docker-runtime/
```

## 3. 构建 runtime 镜像

```bash
docker build -f Dockerfile.runtime-host -t shmtu-cas-ocr-server:gpu-host .
```

这一方案不会在容器里重新编译源码，只是打包宿主机已经构建好的二进制。

## 4. 启动容器并直通核显

运行时挂载：

* `/dev/dri`
* 宿主机模型目录
* 宿主机 Vulkan / 系统库目录

示例命令：

```bash
docker run -d \
  --name shmtu-cas-ocr-gpu-test \
  -p 21620:21600 \
  -p 21621:21601 \
  --device /dev/dri:/dev/dri \
  -v /home/konghaomin/Prj/SHMTU/shmtu-terminal/Server/shmtu-cas-ocr-server/models:/app/models:ro \
  -v /lib/x86_64-linux-gnu:/lib/x86_64-linux-gnu:ro \
  -v /usr/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu:ro \
  -v /usr/share/vulkan:/usr/share/vulkan:ro \
  shmtu-cas-ocr-server:gpu-host
```

说明：

* 容器内服务端监听 `21600` / `21601`
* 映射到宿主机后，对外测试端口为 `21620` / `21621`
* 这里挂载宿主机库目录，是为了快速验证 Vulkan 运行链路

## 5. 检查容器日志

```bash
docker logs shmtu-cas-ocr-gpu-test
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

## 6. 健康检查

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

## 7. 用本地 CLI 测试远程识别

示例命令：

```bash
build/linux-vcpkg-vulkan/ocr/shmtu-cas-ocr-cli/shmtu_cas_ocr_cli \
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

## 8. 清理

停止并删除测试容器：

```bash
docker rm -f shmtu-cas-ocr-gpu-test
```

如果也要删除测试镜像：

```bash
docker rmi shmtu-cas-ocr-server:gpu-host
```

## 关键结论

这次流程验证了以下几点：

* 宿主机 Vulkan 版 `server` / `cli` 可成功构建
* Docker 容器内可以通过 `/dev/dri` 识别 Intel 核显
* 服务端可以在容器中完成模型加载并提供 HTTP OCR 服务
* 本地 `cli` 可以远程连接并完成识别测试

## 当前方案的限制

当前 runtime 验证镜像不是完全自包含的，因为它依赖宿主机挂载：

* `/lib/x86_64-linux-gnu`
* `/usr/lib/x86_64-linux-gnu`
* `/usr/share/vulkan`

因此它适合：

* 本机验证
* 开发联调
* 快速检查 Intel Vulkan 直通是否可用

如果要做真正可分发的 GPU 镜像，建议后续再补一套“自包含运行时依赖打包”方案。
