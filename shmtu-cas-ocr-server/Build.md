# Build Guide

## Tools

You must install the following tools to build the project.

- CMake

## 3rd Party Libraries

- Poco
- Tclap
- OpenCV
- NCNN

## Windows

1. Build OpenCV
2. Build NCNN
3. Build Poco

## Linux

### Ubuntu

```bash
sudo apt install -y libtclap-dev libopencv-dev libpoco-dev libfmt-dev
```

Build or Download NCNN

Install Vulkan SDK

### Arch Linux

```bash
sudo pacman -S opencv poco fmt ncnn tclap

# Vulkan SDK(Optional)
sudo pacman -S vulkan-headers vulkan-validation-layers vulkan-tools
vulkaninfo
```

## macOS

```bash
brew install tclap ncnn protobuf libomp opencv poco fmt
```
