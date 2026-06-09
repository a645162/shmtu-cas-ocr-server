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
#
# The runtime image expects models under <model_dir>/v2/ by default
# (V2 / TriSlot decoder).  The V1 (3-model) layout lives under
# <model_dir>/v1/ and is only bundled when SHMTU_INCLUDE_V1=1 is set
# at build time (e.g. `--build-arg SHMTU_INCLUDE_V1=1`).
# ============================================================
ARG SHMTU_INCLUDE_V1=0
ARG SHMTU_MODEL_DIR=/app/models

FROM runtime-cpu AS runtime-cpu-bundled

COPY --from=builder-cpu /build/models/v2/ ${SHMTU_MODEL_DIR}/v2/
ONBUILD COPY models/v2/ ${SHMTU_MODEL_DIR}/v2/

FROM runtime-gpu AS runtime-gpu-bundled

COPY --from=builder-gpu /build/models/v2/ ${SHMTU_MODEL_DIR}/v2/
ONBUILD COPY models/v2/ ${SHMTU_MODEL_DIR}/v2/
