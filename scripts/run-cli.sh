#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

DEFAULT_BUILD_DIR="${PROJECT_ROOT}/build/linux-vcpkg-vulkan"
FALLBACK_BUILD_DIR="/tmp/shmtu-drogon-vulkan-config"
BUILD_DIR="${SHMTU_BUILD_DIR:-}"

if [[ -z "${BUILD_DIR}" ]]; then
    if [[ -x "${DEFAULT_BUILD_DIR}/shmtu-cas-ocr-cli/shmtu_cas_ocr_cli" ]]; then
        BUILD_DIR="${DEFAULT_BUILD_DIR}"
    else
        BUILD_DIR="${FALLBACK_BUILD_DIR}"
    fi
fi

CLI_BIN="${BUILD_DIR}/shmtu-cas-ocr-cli/shmtu_cas_ocr_cli"
MODEL_DIR="${SHMTU_MODEL_DIR:-${PROJECT_ROOT}/models}"
PRECISION="${SHMTU_PRECISION:-fp16}"

if [[ ! -x "${CLI_BIN}" ]]; then
    echo "CLI binary not found: ${CLI_BIN}" >&2
    echo "Set SHMTU_BUILD_DIR or build the project first." >&2
    exit 1
fi

ARGS=(
    "--model-dir" "${MODEL_DIR}"
    "--precision" "${PRECISION}"
)

if [[ "${SHMTU_USE_GPU:-1}" == "1" ]]; then
    ARGS+=("--use-gpu")
fi

exec "${CLI_BIN}" "${ARGS[@]}" "$@"
