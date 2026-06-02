---
title: SHMTU CAS OCR Server 文档
layout: home

hero:
  name: SHMTU CAS OCR Server
  text: 验证码识别服务器
  tagline: 基于 Drogon + NCNN 的高性能验证码 OCR 识别服务，支持 CPU 与 Vulkan GPU 加速
  actions:
    - theme: brand
      text: 快速开始
      link: /guide/get-started
    - theme: alt
      text: Docker 部署
      link: /guide/docker-deploy
    - theme: alt
      text: API 接口
      link: /guide/api

features:
  - title: 高性能推理
    details: 基于 NCNN 推理引擎，支持 Vulkan GPU 加速，单请求推理延迟在毫秒级别，适合高并发场景。
  - title: 双协议支持
    details: 同时提供 HTTP RESTful API 和 TCP 服务，HTTP 端口默认 21600，TCP 端口默认 21601，灵活适配不同客户端。
  - title: Docker 一键部署
    details: 提供多阶段 Dockerfile，支持 CPU 和 Vulkan GPU 两种运行时目标，配合 docker-compose 可一键启动服务。
  - title: 自动并发调节
    details: Workers、NCNN 线程数、队列容量均支持 0=自动调节模式，根据硬件核心数自动选择最优配置。
  - title: 模型热引导
    details: 容器启动时自动从 Gitee 或 GitHub 下载模型权重，无需手动预置；也支持挂载本地模型目录。
  - title: 健康监控
    details: 提供 /api/health 和 /api/status 端点，内置请求统计、队列状态、运行时长等监控指标，方便接入运维体系。
---

## 项目简介

SHMTU CAS OCR Server 是上海海事大学统一认证平台验证码识别服务器，基于 C++ 开发，使用 [Drogon](https://github.com/drogonframework/drogon) 作为 HTTP/TCP 框架，[NCNN](https://github.com/Tencent/ncnn) 作为推理引擎。

项目采用多目标结构，各模块解耦设计：

| 模块 | 说明 |
|------|------|
| **shmtu-cas-ocr-lib** | 纯 OCR 推理核心库 |
| **shmtu-cas-ocr-server** | Drogon + Trantor 服务端，提供 RESTful API 和 TCP 服务 |
| **shmtu-cas-ocr-cli** | 命令行工具，支持本地和远程识别 |
| **shmtu-cas-ocr-gui** | Qt6 Widgets 桌面 GUI |

`server`、`cli`、`gui` 均依赖 `lib`，不会将 Web 或 GUI 框架反向耦合到 OCR 核心库中。

## 快速链接

- [快速开始](/guide/get-started) -- 从源码构建或 Docker 一键部署
- [Docker 部署](/guide/docker-deploy) -- CPU 和 Vulkan GPU 两种部署模式详解
- [API 接口](/guide/api) -- HTTP 和 TCP 接口完整文档
- [配置参数](/guide/config) -- 命令行参数、环境变量和自动调节规则
- [模型管理](/guide/model-management) -- 模型获取、精度选择和更新方式
- [FAQ](/guide/faq) -- 常见问题解答

## 镜像仓库

Docker 镜像同时发布到以下仓库：

| 仓库 | 地址 |
|------|------|
| Docker Hub | `a645162/shmtu-cas-ocr-server` |
| GHCR | `ghcr.io/a645162/shmtu-cas-ocr-server` |
| 阿里云 ACR | `registry.cn-shanghai.aliyuncs.com/a645162/shmtu-cas-ocr-server` |

## 相关项目

- [shmtu-cas-ocr-model](https://github.com/a645162/shmtu-cas-ocr-model) -- 模型训练代码和权重发布
- [shmtu-cas-ocr-model 数据集](https://huggingface.co/datasets/a645162/shmtu_cas_validate_code) -- 验证码训练数据集
