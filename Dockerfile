# ==========================================================================
# SHMTU CAS OCR Server — Multi-stage Dockerfile (vcpkg manifest)
# Targets:
#   builder       — bootstrap vcpkg and compile
#   runtime-cpu   — CPU runtime
#   runtime-gpu   — Vulkan runtime
# ==========================================================================

FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
ENV VCPKG_ROOT=/opt/vcpkg

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        curl \
        git \
        ninja-build \
        pkg-config \
        unzip \
        zip \
    && rm -rf /var/lib/apt/lists/*

RUN git clone https://github.com/microsoft/vcpkg.git ${VCPKG_ROOT} \
    && ${VCPKG_ROOT}/bootstrap-vcpkg.sh -disableMetrics

WORKDIR /build
COPY . .

ARG VCPKG_MANIFEST_FEATURES=""

RUN cmake --preset linux-vcpkg \
        -DVCPKG_MANIFEST_FEATURES=${VCPKG_MANIFEST_FEATURES} \
    && cmake --build --preset build-linux-vcpkg \
    && cmake --install build/linux-vcpkg --prefix /install

FROM ubuntu:24.04 AS runtime-cpu

ENV DEBIAN_FRONTEND=noninteractive
ENV LD_LIBRARY_PATH=/opt/shmtu/lib

RUN apt-get update && apt-get install -y --no-install-recommends \
        libgomp1 \
        libstdc++6 \
        curl \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /install /opt/shmtu
COPY --from=builder /build/build/linux-vcpkg/vcpkg_installed /opt/shmtu/vcpkg_installed

RUN mkdir -p /app/models

WORKDIR /app

EXPOSE 21600
EXPOSE 21601

ENTRYPOINT ["/opt/shmtu/bin/shmtu_cas_ocr_server"]
CMD ["--model-dir", "/app/models", "--http-port", "21600", "--tcp-port", "21601"]

FROM ubuntu:24.04 AS runtime-gpu

ENV DEBIAN_FRONTEND=noninteractive
ENV LD_LIBRARY_PATH=/opt/shmtu/lib

RUN apt-get update && apt-get install -y --no-install-recommends \
        libgomp1 \
        libstdc++6 \
        libvulkan1 \
        mesa-vulkan-drivers \
        curl \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /install /opt/shmtu
COPY --from=builder /build/build/linux-vcpkg/vcpkg_installed /opt/shmtu/vcpkg_installed

RUN mkdir -p /app/models

WORKDIR /app

EXPOSE 21600
EXPOSE 21601

ENTRYPOINT ["/opt/shmtu/bin/shmtu_cas_ocr_server"]
CMD ["--model-dir", "/app/models", "--http-port", "21600", "--tcp-port", "21601", "--use-gpu"]
