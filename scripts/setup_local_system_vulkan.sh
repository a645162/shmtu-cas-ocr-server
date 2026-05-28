#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "[build] Building via Docker builder image with chsrc enabled..."
cd "${PROJECT_ROOT}"
USE_CHSRC=1 ./scripts/ci_build_system_vulkan.sh
