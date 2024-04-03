# 上海海事大学 统一认证平台 验证码识别服务器

ShangHai Maritime University CAS OCR Server

## 本系列项目

### 客户端

* Go Wails版
https://github.com/a645162/SHMTU-Terminal-Wails
* Rust Tauri版(或许以后会做吧~)

### 服务器部署模型

验证码OCR识别系列项目今后将只会维护推理服务器(shmtu-cas-ocr-server)这一个项目。

https://github.com/a645162/shmtu-cas-ocr-server

### 统一认证登录流程(数字平台+微信平台)

* Kotlin版(方便移植Android)
https://github.com/a645162/shmtu-cas-kotlin
* Go版(为Wails桌面客户端做准备)
https://github.com/a645162/shmtu-cas-go
* Rust版(未来想做Tauri桌面客户端可能会移植)

### 模型训练(不再维护)

使用PyTorch以及经典网络ResNet

https://github.com/a645162/shmtu-cas-ocr-model

### 模型本地部署(不再维护)

* Windows客户端(包括VC Win32 GUI以及C# WPF)
https://github.com/a645162/shmtu-cas-ocr-demo-windows
* Qt客户端(支持Windows/macOS/Linux)
https://github.com/a645162/shmtu-cas-ocr-demo-qt
* Android客户端
https://github.com/a645162/shmtu-cas-demo-android

## Build

[Build Guide](Build.md)
