#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

DEFAULT_BUILD_DIR="${PROJECT_ROOT}/build/linux-vcpkg-vulkan"
FALLBACK_BUILD_DIR="/tmp/shmtu-drogon-vulkan-config"
BUILD_DIR="${SHMTU_BUILD_DIR:-}"

if [[ -z "${BUILD_DIR}" ]]; then
    if [[ -x "${DEFAULT_BUILD_DIR}/shmtu-cas-ocr-gui/shmtu_cas_ocr_gui" ]]; then
        BUILD_DIR="${DEFAULT_BUILD_DIR}"
    else
        BUILD_DIR="${FALLBACK_BUILD_DIR}"
    fi
fi

GUI_BIN="${BUILD_DIR}/shmtu-cas-ocr-gui/shmtu_cas_ocr_gui"

if [[ ! -x "${GUI_BIN}" ]]; then
    echo "GUI binary not found: ${GUI_BIN}" >&2
    echo "Set SHMTU_BUILD_DIR or build the project first." >&2
    exit 1
fi

exec "${GUI_BIN}" "$@"
