# Ubuntu System Dependencies for vcpkg Builds

This project now uses `vcpkg` for C/C++ package management, but several Linux
system tools are still required by some upstream ports during dependency
resolution and source builds.

This document records the `apt` packages that were confirmed as necessary while
bringing up the current stack:

* `Drogon + Trantor`
* `ncnn` with `Vulkan`
* `OpenCV`
* `Qt6`

## Confirmed Required Packages

Install these packages before running `cmake` with the `vcpkg` toolchain:

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
