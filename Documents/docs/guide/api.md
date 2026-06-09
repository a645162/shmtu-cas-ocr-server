---
title: API接口
---

# API 接口

SHMTU CAS OCR Server 同时提供 HTTP RESTful API 和 TCP 服务，默认端口分别为 21600 和 21601。

## HTTP API

### 健康检查

```
GET /api/health
```

返回服务健康状态，适合用于负载均衡器或监控系统探测。

**响应示例：**

```json
{
  "status": "healthy",
  "modelsLoaded": true,
  "poolSize": 2,
  "serverName": "my-ocr-node"
}
```

**字段说明：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `status` | string | 服务状态：`healthy` 或 `unhealthy` |
| `modelsLoaded` | boolean | 模型是否已加载 |
| `poolSize` | number | Worker 池大小 |
| `serverName` | string | 服务名称（可选，通过 `SHMTU_SERVER_NAME` 配置） |

---

### 服务状态

```
GET /api/status
```

返回服务的详细运行状态和统计数据。

**响应示例：**

```json
{
  "status": "healthy",
  "availabilityLevel": "available",
  "reason": "",
  "modelsLoaded": true,
  "poolSize": 2,
  "queueCapacity": 32,
  "pendingRequests": 0,
  "activeWorkers": 1,
  "totalRequests": 1234,
  "successCount": 1200,
  "failureCount": 34,
  "uptimeSeconds": 86400,
  "serverName": "my-ocr-node"
}
```

**字段说明：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `status` | string | 服务状态：`healthy` 或 `unavailable` |
| `availabilityLevel` | string | 可用级别：`available`、`busy`、`unavailable` |
| `reason` | string | 不可用原因（正常时为空） |
| `modelsLoaded` | boolean | 模型是否已加载 |
| `poolSize` | number | Worker 池大小 |
| `queueCapacity` | number | 队列容量 |
| `pendingRequests` | number | 当前排队请求数 |
| `activeWorkers` | number | 活跃 Worker 数 |
| `totalRequests` | number | 总请求数 |
| `successCount` | number | 成功请求数 |
| `failureCount` | number | 失败请求数 |
| `uptimeSeconds` | number | 服务运行时长（秒） |
| `serverName` | string | 服务名称（可选） |

当 `pendingRequests > queueCapacity / 2` 时，`availabilityLevel` 为 `busy`。当模型未加载时，`availabilityLevel` 为 `unavailable`，`reason` 字段会显示 "Models not loaded"。

---

### OCR 识别（JSON）

```
POST /api/ocr
Content-Type: application/json
```

通过 JSON Body 传入 Base64 编码的验证码图片进行识别。可选 `version` 字段在请求级别覆盖默认模型版本（`v1` / `v2`）。

**请求体：**

```json
{
  "imageBase64": "<base64 编码的图片数据>",
  "version": "v2"
}
```

**成功响应（200）：**

```json
{
  "success": true,
  "expression": "3+5",
  "result": 8,
  "equalSymbol": 1,
  "operator": 0,
  "digit1": 3,
  "digit2": 5,
  "error": null
}
```

**失败响应（200，识别失败）：**

```json
{
  "success": false,
  "expression": "",
  "result": 0,
  "equalSymbol": 0,
  "operator": 0,
  "digit1": 0,
  "digit2": 0,
  "error": "Recognition failed"
}
```

**请求格式错误（400）：**

```json
{
  "success": false,
  "error": "Invalid JSON body"
}
```

**服务过载（503）：**

```json
{
  "success": false,
  "error": "Server overloaded"
}
```

**响应字段说明：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `success` | boolean | 识别是否成功 |
| `expression` | string | 识别出的算式表达式（如 `3+5`） |
| `result` | number | 算式计算结果 |
| `equalSymbol` | number | 等号样式：`0` = 中文等号，`1` = 标准等号 `=` |
| `operator` | number | 运算符类型（见下表） |
| `digit1` | number | 第一个数字 |
| `digit2` | number | 第二个数字 |
| `error` | string \| null | 错误信息（成功时为 null） |

**运算符类型（operator 字段）：**

| 值 | 含义 |
|----|------|
| 0 | 加号 `+` |
| 1 | 中文加号 |
| 2 | 减号 `-` |
| 3 | 中文减号 |
| 4 | 乘号 `*` |
| 5 | 中文乘号 |

---

### OCR 识别（文件上传）

```
POST /api/ocr/upload
Content-Type: multipart/form-data
```

通过 multipart 表单上传验证码图片文件。

**请求格式：**

```
POST /api/ocr/upload
Content-Type: multipart/form-data

--boundary
Content-Disposition: form-data; name="file"; filename="captcha.png"
Content-Type: image/png

<图片二进制数据>
--boundary--
```

表单字段名必须为 `file`。如果未提供文件，返回 400 错误。

**响应格式：** 与 `/api/ocr` 接口相同。

---

## TCP 服务

TCP 服务默认监听端口 21601，适用于需要长连接或更低延迟的场景。

### 协议格式

TCP 协议采用简单的文本帧格式：

1. 客户端发送验证码图片的原始二进制数据
2. 数据以 `<END>` 标记结尾
3. 服务端返回识别结果字符串后关闭连接

**请求格式：**

```
<图片二进制数据><END>
```

**成功响应：**

服务端返回识别出的表达式字符串，例如：

```
3+5
```

**失败响应：**

服务端返回空字符串：

```
(空)
```

**队列满时：**

服务端返回空字符串并立即关闭连接。

### 协议示例

```
客户端 → 服务端:  <PNG 图片二进制数据><END>
服务端 → 客户端:  3+5
(服务端关闭连接)
```

### 连接生命周期

- 每次识别使用一个独立的 TCP 连接
- 服务端发送完响应后会主动关闭连接
- 如果队列已满，服务端返回空字符串后立即关闭

## 调用示例

### cURL 示例

```bash
# 健康检查
curl http://localhost:21600/api/health

# 服务状态
curl http://localhost:21600/api/status

# 使用 Base64 JSON 识别
curl -X POST http://localhost:21600/api/ocr \
  -H "Content-Type: application/json" \
  -d '{"imageBase64": "'"$(base64 -w0 captcha.png)"'"}'

# 使用文件上传识别
curl -X POST http://localhost:21600/api/ocr/upload \
  -F "file=@captcha.png"
```

### Python 示例

```python
import base64
import requests

# Base64 方式
with open("captcha.png", "rb") as f:
    img_b64 = base64.b64encode(f.read()).decode()

resp = requests.post("http://localhost:21600/api/ocr", json={
    "imageBase64": img_b64
})
result = resp.json()
if result["success"]:
    print(f"识别结果: {result['expression']} = {result['result']}")
else:
    print(f"识别失败: {result['error']}")

# 文件上传方式
with open("captcha.png", "rb") as f:
    resp = requests.post("http://localhost:21600/api/ocr/upload", files={
        "file": f
    })
    print(resp.json())
```

### Python TCP 示例

```python
import socket

with open("captcha.png", "rb") as f:
    image_data = f.read()

# 发送图片数据 + <END> 标记
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(("127.0.0.1", 21601))
sock.sendall(image_data + b"<END>")

# 接收响应
response = b""
while True:
    chunk = sock.recv(4096)
    if not chunk:
        break
    response += chunk

sock.close()
result = response.decode("utf-8")
if result:
    print(f"识别结果: {result}")
else:
    print("识别失败或服务过载")
```

## 错误处理

### HTTP 状态码

| HTTP 状态码 | 说明 |
|-------------|------|
| 200 | 请求成功（注意：识别失败也返回 200，通过 `success` 字段区分） |
| 400 | 请求格式错误（无效 JSON、缺少 imageBase64、无效 multipart） |
| 503 | 服务过载，排队请求已满 |

### TCP 行为

| 场景 | 响应 |
|------|------|
| 识别成功 | 返回表达式字符串，关闭连接 |
| 识别失败 | 返回空字符串，关闭连接 |
| 队列已满 | 返回空字符串，关闭连接 |

建议客户端实现重试逻辑，在收到 503 响应或 TCP 空响应时等待短暂时间后重试。

## 性能指标

服务端在每次请求完成后会记录以下性能指标到日志：

| 指标 | 说明 |
|------|------|
| `queue_wait_ms` | 请求在队列中等待的时间（毫秒） |
| `inference_ms` | NCNN 推理耗时（毫秒） |
| `total_ms` | 请求总耗时（毫秒） |
| `input_bytes` | 输入图片字节数 |
| `worker_index` | 处理该请求的 Worker 编号 |

这些指标可通过查看日志获取，用于性能调优和容量规划。
