#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
VERSION_FILE="${PROJECT_ROOT}/VERSION"

usage() {
  cat <<'EOF'
Usage:
  stage_binaries.sh --build-dir <dir> [--docker-dir <dir>] [--release-dir <dir>] \
    [--platform-id <id>] [--variant <cpu|vulkan>]

Examples:
  stage_binaries.sh --build-dir build/linux-system-vulkan --docker-dir docker-runtime
  stage_binaries.sh --build-dir build/linux-system \
    --release-dir release-assets/cpu \
    --platform-id ubuntu2404-linux-x64 \
    --variant cpu
EOF
}

BUILD_DIR=""
DOCKER_DIR=""
RELEASE_DIR=""
PLATFORM_ID="ubuntu2404-linux-x64"
VARIANT=""

while [ "$#" -gt 0 ]; do
  case "$1" in
    --build-dir)
      BUILD_DIR="$2"
      shift 2
      ;;
    --docker-dir)
      DOCKER_DIR="$2"
      shift 2
      ;;
    --release-dir)
      RELEASE_DIR="$2"
      shift 2
      ;;
    --platform-id)
      PLATFORM_ID="$2"
      shift 2
      ;;
    --variant)
      VARIANT="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if [ -z "${BUILD_DIR}" ]; then
  echo "--build-dir is required." >&2
  usage >&2
  exit 1
fi

if [ -z "${DOCKER_DIR}" ] && [ -z "${RELEASE_DIR}" ]; then
  echo "At least one of --docker-dir or --release-dir is required." >&2
  usage >&2
  exit 1
fi

VERSION="$(tr -d '\n\r' < "${VERSION_FILE}")"
if ! [[ "${VERSION}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "Invalid semantic version in ${VERSION_FILE}: ${VERSION}" >&2
  exit 1
fi

resolve_binary() {
  local target_dir="$1"
  local binary_name="$2"
  local versioned_path="${BUILD_DIR}/${target_dir}/${binary_name}-${VERSION}"
  local canonical_path="${BUILD_DIR}/${target_dir}/${binary_name}"

  if [ -f "${versioned_path}" ]; then
    printf '%s\n' "${versioned_path}"
    return 0
  fi

  if [ -L "${canonical_path}" ] || [ -f "${canonical_path}" ]; then
    printf '%s\n' "${canonical_path}"
    return 0
  fi

  echo "Cannot find built binary for ${binary_name} in ${BUILD_DIR}/${target_dir}" >&2
  exit 1
}

stage_binary() {
  local target_dir="$1"
  local binary_name="$2"
  local src_path
  local release_name

  src_path="$(resolve_binary "${target_dir}" "${binary_name}")"
  src_path="$(readlink -f "${src_path}")"

  if [ -n "${DOCKER_DIR}" ]; then
    install -D -m 0755 "${src_path}" "${DOCKER_DIR}/${binary_name}"
    echo "Staged for Docker: ${DOCKER_DIR}/${binary_name}"
  fi

  if [ -n "${RELEASE_DIR}" ]; then
    release_name="${binary_name}-${VERSION}-${PLATFORM_ID}"
    if [ -n "${VARIANT}" ]; then
      release_name="${release_name}-${VARIANT}"
    fi
    install -D -m 0755 "${src_path}" "${RELEASE_DIR}/${release_name}"
    echo "Staged for release: ${RELEASE_DIR}/${release_name}"
  fi
}

stage_binary "ocr/shmtu-cas-ocr-server" "shmtu_cas_ocr_server"
stage_binary "ocr/shmtu-cas-ocr-cli" "shmtu_cas_ocr_cli"
stage_binary "ocr/shmtu-cas-ocr-tui" "shmtu_cas_ocr_tui"
