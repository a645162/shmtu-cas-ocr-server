# ==========================================================================
# SHMTU CAS OCR Server — Multi-stage Dockerfile
# Targets:
#   builder       — compile everything
#   runtime-cpu   — minimal CPU-only runtime
#   runtime-gpu   — NVIDIA CUDA + Vulkan runtime
# ==========================================================================

# --------------------------------------------------------------------------
# Stage 1: Builder
# --------------------------------------------------------------------------
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        git \
        pkg-config \
        libopencv-dev \
        libpoco-dev \
        libfmt-dev \
        libvulkan-dev \
        vulkan-tools \
    && rm -rf /var/lib/apt/lists/*

# Build NCNN from source (with Vulkan support)
ARG NCNN_VERSION=20240102
RUN git clone --depth 1 --branch ${NCNN_VERSION} \
        https://github.com/Tencent/ncnn.git /tmp/ncnn-src \
    && mkdir -p /tmp/ncnn-build \
    && cd /tmp/ncnn-build \
    && cmake /tmp/ncnn-src \
        -DCMAKE_BUILD_TYPE=Release \
        -DNCNN_VULKAN=ON \
        -DNCNN_BUILD_EXAMPLES=OFF \
        -DNCNN_BUILD_TESTS=OFF \
        -DNCNN_BUILD_TOOLS=OFF \
    && cmake --build . -j$(nproc) \
    && cmake --install . --prefix /usr/local \
    && rm -rf /tmp/ncnn-src /tmp/ncnn-build

# Copy source
WORKDIR /build
COPY . .

# Build the project
ARG BUILD_TARGET=cpu
RUN mkdir -p build && cd build \
    && cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DUSE_VULKAN=$( [ "$BUILD_TARGET" = "gpu" ] && echo "ON" || echo "OFF" ) \
        -DBUILD_SERVER=ON \
        -DBUILD_CLI=ON \
    && cmake --build . -j$(nproc) \
    && cmake --install . --prefix /install

# --------------------------------------------------------------------------
# Stage 2: CPU runtime
# --------------------------------------------------------------------------
FROM ubuntu:22.04 AS runtime-cpu

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        libopencv-core4.5d \
        libopencv-imgproc4.5d \
        libopencv-imgcodecs4.5d \
        libpocofoundation80 \
        libpoconet80 \
        libfmt8 \
        curl \
    && rm -rf /var/lib/apt/lists/*

# Copy built artifacts
COPY --from=builder /install/bin/ /usr/local/bin/
COPY --from=builder /install/lib/ /usr/local/lib/
COPY --from=builder /usr/local/lib/libncnn* /usr/local/lib/

RUN ldconfig

# Create models directory
RUN mkdir -p /app/models

WORKDIR /app

# HTTP port (RESTful API)
EXPOSE 21600
# TCP port (legacy protocol)
EXPOSE 21601

ENTRYPOINT ["shmtu_cas_ocr_server"]
CMD ["--model-dir", "/app/models", "--http-port", "21600", "--tcp-port", "21601"]

# --------------------------------------------------------------------------
# Stage 3: GPU runtime (NVIDIA CUDA + Vulkan)
# --------------------------------------------------------------------------
FROM nvidia/cuda:12.4.1-runtime-ubuntu22.04 AS runtime-gpu

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        libopencv-core4.5d \
        libopencv-imgproc4.5d \
        libopencv-imgcodecs4.5d \
        libpocofoundation80 \
        libpoconet80 \
        libfmt8 \
        libvulkan1 \
        mesa-vulkan-drivers \
        curl \
    && rm -rf /var/lib/apt/lists/*

# Copy built artifacts
COPY --from=builder /install/bin/ /usr/local/bin/
COPY --from=builder /install/lib/ /usr/local/lib/
COPY --from=builder /usr/local/lib/libncnn* /usr/local/lib/

RUN ldconfig

# Create models directory
RUN mkdir -p /app/models

WORKDIR /app

# HTTP port (RESTful API)
EXPOSE 21600
# TCP port (legacy protocol)
EXPOSE 21601

ENTRYPOINT ["shmtu_cas_ocr_server"]
CMD ["--model-dir", "/app/models", "--http-port", "21600", "--tcp-port", "21601", "--use-gpu"]
