#!/usr/bin/env bash

set -euo pipefail

SERVER_BIN="${SHMTU_SERVER_BIN:-/opt/shmtu/bin/shmtu_cas_ocr_server}"
DOWNLOAD_MODELS_BIN="${SHMTU_DOWNLOAD_MODELS_BIN:-/opt/shmtu/bin/docker_download_models.sh}"
LOG_DIR="${SHMTU_LOG_DIR:-/app/logs}"
LOG_FILE="${SHMTU_LOG_FILE:-${LOG_DIR%/}/shmtu-cas-ocr-server.log}"
SERVER_ARGS=()

mkdir -p "${LOG_DIR}" "$(dirname "${LOG_FILE}")"
touch "${LOG_FILE}"
exec > >(tee -a "${LOG_FILE}") 2>&1

log() {
  printf '[%s] %s\n' "$(date '+%Y-%m-%d %H:%M:%S%z')" "$*"
}

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

log "Runtime entrypoint initialized"
log "  server_bin=${SERVER_BIN}"
log "  download_models_bin=${DOWNLOAD_MODELS_BIN}"
log "  log_dir=${LOG_DIR}"
log "  log_file=${LOG_FILE}"
log "  model_dir=${SHMTU_MODEL_DIR:-/app/models}"
log "  model_source=${SHMTU_MODEL_SOURCE:-github}"
log "  auto_download_models=${SHMTU_AUTO_DOWNLOAD_MODELS:-1}"
log "  server_args=${SERVER_ARGS[*]:-<none>}"

bash "${DOWNLOAD_MODELS_BIN}" "${SERVER_ARGS[@]}"

log "Starting OCR server"
exec stdbuf -oL -eL "${SERVER_BIN}" "${SERVER_ARGS[@]}"
