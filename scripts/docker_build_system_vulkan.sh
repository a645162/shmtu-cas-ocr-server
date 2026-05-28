#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

USE_CHSRC="${USE_CHSRC:-0}"
BUILDER_IMAGE="${BUILDER_IMAGE:-shmtu-cas-ocr-builder:system-vulkan}"
RUNTIME_IMAGE="${RUNTIME_IMAGE:-shmtu-cas-ocr-server:system-vulkan}"
NCNN_FORCE="${NCNN_FORCE:-0}"
HOST_UID="$(id -u)"
HOST_GID="$(id -g)"

cd "${PROJECT_ROOT}"

echo "[1/5] Downloading prebuilt ncnn..."
if [ "${NCNN_FORCE}" = "1" ]; then
  python3 3rdparty/NCNN/download_ncnn.py --force
else
  python3 3rdparty/NCNN/download_ncnn.py
fi

echo "[2/5] Building Docker builder image..."
docker build \
  -f Dockerfile.builder-system \
  --build-arg USE_CHSRC="${USE_CHSRC}" \
  -t "${BUILDER_IMAGE}" \
  .

echo "[3/5] Building server and CLI inside Docker..."
docker run --rm \
  -v "${PROJECT_ROOT}:/work" \
  -w /work \
  "${BUILDER_IMAGE}" \
  bash -lc '
    set -euo pipefail
    rm -rf build/linux-system-vulkan docker-runtime
  '

docker run --rm \
  --user "${HOST_UID}:${HOST_GID}" \
  -e HOME=/tmp \
  -v "${PROJECT_ROOT}:/work" \
  -w /work \
  "${BUILDER_IMAGE}" \
  bash -lc '
    set -euo pipefail
    mkdir -p docker-runtime
    cmake --preset linux-system-vulkan
    cmake --build --preset build-linux-system-vulkan --target shmtu_cas_ocr_server shmtu_cas_ocr_cli
    cp build/linux-system-vulkan/ocr/shmtu-cas-ocr-server/shmtu_cas_ocr_server docker-runtime/
    cp build/linux-system-vulkan/ocr/shmtu-cas-ocr-cli/shmtu_cas_ocr_cli docker-runtime/
  '

echo "[4/5] Building runtime image from copied artifacts..."
docker build -f Dockerfile.runtime-system -t "${RUNTIME_IMAGE}" .

echo "[5/5] Done."
echo "Binaries:"
echo "  ${PROJECT_ROOT}/build/linux-system-vulkan/ocr/shmtu-cas-ocr-server/shmtu_cas_ocr_server"
echo "  ${PROJECT_ROOT}/build/linux-system-vulkan/ocr/shmtu-cas-ocr-cli/shmtu_cas_ocr_cli"
echo "Runtime image:"
echo "  ${RUNTIME_IMAGE}"
