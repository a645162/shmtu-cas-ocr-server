# Ubuntu System Dependencies

This project supports two Linux dependency paths:

1. `vcpkg` for the full dependency stack
2. Ubuntu `apt` for most libraries, plus a prebuilt Tencent `ncnn` package

This document records the `apt` packages that are useful for both setups.

These packages were confirmed as useful while bringing up the current stack:

* `Drogon + Trantor`
* `ncnn` with `Vulkan`
* `OpenCV`
* `Qt6`

## Recommended Ubuntu Packages

If you want the “system packages + prebuilt ncnn” path, install:

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  cmake \
  ninja-build \
  pkg-config \
  python3 \
  ca-certificates \
  libopencv-dev \
  libfmt-dev \
  libdrogon-dev \
  libtrantor-dev \
  libjsoncpp-dev \
  default-libmysqlclient-dev \
  libhiredis-dev \
  libssl-dev \
  libcurl4-openssl-dev \
  qt6-base-dev \
  libvulkan-dev \
  vulkan-tools \
  mesa-vulkan-drivers
```

Then download the prebuilt Ubuntu 24.04 `ncnn` package:

```bash
python3 3rdparty/NCNN/download_ncnn.py
```

Then configure and build:

```bash
cmake --preset linux-system-vulkan
cmake --build --preset build-linux-system-vulkan
```

If you want local and CI to share the same build pipeline, prefer the Docker builder flow instead:

```bash
./scripts/ci_build_system_vulkan.sh
```

For local Ubuntu machines that need a faster apt mirror inside the builder image:

```bash
./scripts/setup_local_system_vulkan.sh
```

Notes:

* `libdrogon-dev` on Ubuntu depends on the MySQL client development files being present at configure time
* `libdrogon-dev` on Ubuntu also depends on `hiredis` development files being present at configure time
* this project pre-seeds the standard Ubuntu MySQL include/library paths for the `linux-system*` presets to work around Drogon's legacy `FindMySQL.cmake`

If you want the `vcpkg` path, install these extra system tools before running `cmake` with the `vcpkg` toolchain:

```bash
sudo apt-get update
sudo apt-get install -y \
  bison \
  autoconf \
  autoconf-archive \
  automake \
  libtool \
  libx11-dev \
  libxft-dev \
  libxext-dev \
  libgles2-mesa-dev \
  libxi-dev \
  libxtst-dev \
  libwayland-dev \
  libxkbcommon-dev \
  libegl1-mesa-dev \
  libibus-1.0-dev
```

## Why They Are Needed

### `bison`

Confirmed required by the `gettext` port during `vcpkg install`.

Without it, `vcpkg` stops while building transitive dependencies pulled in by
the Linux GUI and OpenCV stack.

### `autoconf`, `autoconf-archive`, `automake`, `libtool`

Confirmed required by the `gperf` port during `vcpkg install`.

`gperf` is part of the transitive dependency chain when resolving the current
Linux desktop stack used by GUI-related packages.

### `libx11-dev`, `libxft-dev`, `libxext-dev`

Reported by the `cairo` port when building with the `x11` feature enabled.

These are Xorg development packages from Ubuntu, and they are needed by the
Linux desktop GUI dependency chain.

### `libgles2-mesa-dev`

Reported by the `libepoxy` port.

This package is needed by the Linux OpenGL / GUI dependency chain.

### `libxi-dev`, `libxtst-dev`

Reported by the `at-spi2-core` port.

These packages are part of the Linux accessibility / desktop GUI stack on Ubuntu.

### `libwayland-dev`, `libxkbcommon-dev`, `libegl1-mesa-dev`

Reported by the `sdl2` port when its `wayland` feature is enabled.

These packages are part of the Linux windowing / input / EGL stack that can be
pulled in transitively by desktop GUI dependencies.

### `libibus-1.0-dev`

Reported by the `sdl2` port when its `ibus` feature is enabled.

This package is part of the Linux input method stack used by some desktop GUI
dependencies.

## Scope

These packages are system-level build tools. They are not replacements for the
libraries managed by `vcpkg`, and they should be installed with `apt`, not
added to `vcpkg.json`.

## Related Project Dependencies

The current manifest-managed project stack includes:

* `drogon`
* `trantor`
* `ncnn`
* `opencv4`
* `fmt`
* `Qt6` from Ubuntu `apt`

Vulkan support is enabled through the manifest feature:

* `ncnn[vulkan]`

## Recommended Build Flow

After the system packages above are installed:

```bash
cmake -S . -B build/linux-vcpkg-vulkan-gui \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_MANIFEST_MODE=ON \
  -DVCPKG_FEATURE_FLAGS=manifests \
  -DVCPKG_MANIFEST_FEATURES=vulkan \
  -DUSE_VULKAN=ON \
  -DBUILD_SERVER=ON \
  -DBUILD_CLI=ON \
  -DBUILD_GUI=ON
```

Then build:

```bash
cmake --build build/linux-vcpkg-vulkan-gui
```
