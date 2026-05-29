#!/usr/bin/env bash

set -euo pipefail

SERVER_BIN="${SHMTU_SERVER_BIN:-/opt/shmtu/bin/shmtu_cas_ocr_server}"
DOWNLOAD_MODELS_BIN="${SHMTU_DOWNLOAD_MODELS_BIN:-/opt/shmtu/bin/docker_download_models.sh}"
SERVER_ARGS=()

while [ "$#" -gt 0 ]; do
  case "$1" in
    --model-source)
      if [ "$#" -lt 2 ]; then
        echo "Missing value for --model-source" >&2
        exit 1
      fi
      export SHMTU_MODEL_SOURCE="$2"
      shift 2
      ;;
    --model-base-url)
      if [ "$#" -lt 2 ]; then
        echo "Missing value for --model-base-url" >&2
        exit 1
      fi
      export SHMTU_MODEL_BASE_URL="$2"
      shift 2
      ;;
    --model-fallback-base-url)
      if [ "$#" -lt 2 ]; then
        echo "Missing value for --model-fallback-base-url" >&2
        exit 1
      fi
      export SHMTU_MODEL_FALLBACK_BASE_URL="$2"
      shift 2
      ;;
    --auto-download-models)
      if [ "$#" -lt 2 ]; then
        echo "Missing value for --auto-download-models" >&2
        exit 1
      fi
      export SHMTU_AUTO_DOWNLOAD_MODELS="$2"
      shift 2
      ;;
    *)
      SERVER_ARGS+=("$1")
      shift
      ;;
  esac
done

bash "${DOWNLOAD_MODELS_BIN}" "${SERVER_ARGS[@]}"

exec "${SERVER_BIN}" "${SERVER_ARGS[@]}"
