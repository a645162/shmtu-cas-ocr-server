#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

PRESET="${SHMTU_BUILD_PRESET:-build-linux-vcpkg-vulkan}"

echo "shmtu-cas-ocr-lib is a library target and has no standalone executable."
echo "Running a focused build check for the library through preset: ${PRESET}"

exec cmake --build --preset "${PRESET}" --target shmtu_cas_ocr_lib "$@"
