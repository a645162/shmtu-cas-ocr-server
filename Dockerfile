# ==========================================================================
# SHMTU CAS OCR Server — Multi-stage Dockerfile (vcpkg manifest)
# Targets:
#   builder-cpu   — CPU build
#   builder-gpu   — Vulkan build
#   runtime-cpu   — CPU runtime
#   runtime-gpu   — Vulkan runtime
# ==========================================================================

FROM ubuntu:24.04 AS builder-base

ENV DEBIAN_FRONTEND=noninteractive
ENV VCPKG_ROOT=/opt/vcpkg

RUN apt-get update \
    && apt-get upgrade -y \
    && apt-get install -y --no-install-recommends \
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

FROM builder-base AS builder-cpu

RUN cmake --preset linux-vcpkg \
    && cmake --build --preset build-linux-vcpkg \
    && cmake --install build/linux-vcpkg --prefix /install

FROM builder-base AS builder-gpu

RUN cmake --preset linux-vcpkg-vulkan \
    && cmake --build --preset build-linux-vcpkg-vulkan \
    && cmake --install build/linux-vcpkg-vulkan --prefix /install

FROM ubuntu:24.04 AS runtime-cpu

ENV DEBIAN_FRONTEND=noninteractive
ENV LD_LIBRARY_PATH=/opt/shmtu/lib

RUN apt-get update \
    && apt-get upgrade -y \
    && apt-get install -y --no-install-recommends \
        libgomp1 \
        libstdc++6 \
        curl \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder-cpu /install /opt/shmtu
COPY --from=builder-cpu /build/build/linux-vcpkg/vcpkg_installed /opt/shmtu/vcpkg_installed
COPY scripts/docker_download_models.sh /opt/shmtu/bin/docker_download_models.sh
COPY scripts/docker_runtime_entrypoint.sh /opt/shmtu/bin/docker_runtime_entrypoint.sh

RUN mkdir -p /app/models /app/logs
RUN chmod +x /opt/shmtu/bin/docker_download_models.sh /opt/shmtu/bin/docker_runtime_entrypoint.sh

WORKDIR /app

EXPOSE 21600
EXPOSE 21601

ENTRYPOINT ["/opt/shmtu/bin/docker_runtime_entrypoint.sh"]
CMD ["--model-dir", "/app/models", "--http-port", "21600", "--tcp-port", "21601"]

FROM ubuntu:24.04 AS runtime-gpu

ENV DEBIAN_FRONTEND=noninteractive
ENV LD_LIBRARY_PATH=/opt/shmtu/lib

RUN apt-get update \
    && apt-get upgrade -y \
    && apt-get install -y --no-install-recommends \
        libgomp1 \
        libstdc++6 \
        libvulkan1 \
        mesa-vulkan-drivers \
        curl \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder-gpu /install /opt/shmtu
COPY --from=builder-gpu /build/build/linux-vcpkg-vulkan/vcpkg_installed /opt/shmtu/vcpkg_installed
COPY scripts/docker_download_models.sh /opt/shmtu/bin/docker_download_models.sh
COPY scripts/docker_runtime_entrypoint.sh /opt/shmtu/bin/docker_runtime_entrypoint.sh

RUN mkdir -p /app/models /app/logs
RUN chmod +x /opt/shmtu/bin/docker_download_models.sh /opt/shmtu/bin/docker_runtime_entrypoint.sh

WORKDIR /app

EXPOSE 21600
EXPOSE 21601

ENTRYPOINT ["/opt/shmtu/bin/docker_runtime_entrypoint.sh"]
CMD ["--model-dir", "/app/models", "--http-port", "21600", "--tcp-port", "21601", "--use-gpu"]

# ============================================================
# Bundled variants (with model weights included)
# ============================================================
FROM runtime-cpu AS runtime-cpu-bundled

COPY models/ /app/models/

FROM runtime-gpu AS runtime-gpu-bundled

COPY models/ /app/models/
