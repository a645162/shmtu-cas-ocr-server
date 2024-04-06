# 上海海事大学 统一认证平台 验证码识别服务器

ShangHai Maritime University CAS OCR Server

## 模型权重

请前往[shmtu-cas-ocr-model](https://github.com/a645162/shmtu-cas-ocr-model)项目的[Github Release](https://github.com/a645162/shmtu-cas-ocr-model/releases)中下载NCNN版权重。

## Build

[Build构建指南](shmtu-cas-ocr-server/Build.md)

**注意：**
Windows下构建比较麻烦，建议使用Vcpkg或者不要使用Windows。

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
* Golang版(为Wails桌面客户端做准备)
https://github.com/a645162/shmtu-cas-go
* Rust版(未来想做Tauri桌面客户端可能会移植)
ps.功能其实和Golang版本没啥区别，甚至可能实现地更费劲，Golang的移植已经让我比较抓狂了，虽然Rust我也是会的，但是或许不会做。。。

### 模型训练(不再维护)

**模型训练**

使用PyTorch以及经典网络ResNet

https://github.com/a645162/shmtu-cas-ocr-model

**人工标注的数据集(2选1下载)**

* Hugging Face
https://huggingface.co/datasets/a645162/shmtu_cas_validate_code
* Gitee AI(国内较快)
https://ai.gitee.com/datasets/a645162/shmtu_cas_validate_code

训练代码中包含爬虫代码，以及自动测试识别结果代码。
您可以对其修改，对测试通过的图片进行标注，这样可以获得准确的标注。

### 模型本地部署Demo(不再维护)

* Windows客户端(包括VC Win32 GUI以及C# WPF)
https://github.com/a645162/shmtu-cas-ocr-demo-windows
* Qt客户端(支持Windows/macOS/Linux)
https://github.com/a645162/shmtu-cas-ocr-demo-qt
* Android客户端
https://github.com/a645162/shmtu-cas-demo-android
