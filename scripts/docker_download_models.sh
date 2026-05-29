#!/usr/bin/env bash

set -euo pipefail

MODEL_DIR="${SHMTU_MODEL_DIR:-/app/models}"
PRECISION="${SHMTU_PRECISION:-fp16}"
AUTO_DOWNLOAD_MODELS="${SHMTU_AUTO_DOWNLOAD_MODELS:-1}"
MODEL_SOURCE="${SHMTU_MODEL_SOURCE:-github}"
PRIMARY_BASE_URL="${SHMTU_MODEL_BASE_URL:-https://github.com/a645162/shmtu-cas-ocr-model/releases/download/v1.0-NCNN}"
FALLBACK_BASE_URL="${SHMTU_MODEL_FALLBACK_BASE_URL:-https://gitee.com/a645162/shmtu-cas-ocr-model/releases/download/v1.0-NCNN}"

log() {
  printf '[%s] %s\n' "$(date '+%Y-%m-%d %H:%M:%S%z')" "$*"
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --model-dir)
      if [ "$#" -lt 2 ]; then
        echo "Missing value for --model-dir" >&2
        exit 1
      fi
      MODEL_DIR="$2"
      shift 2
      ;;
    --precision)
      if [ "$#" -lt 2 ]; then
        echo "Missing value for --precision" >&2
        exit 1
      fi
      PRECISION="$2"
      shift 2
      ;;
    --model-source)
      if [ "$#" -lt 2 ]; then
        echo "Missing value for --model-source" >&2
        exit 1
      fi
      MODEL_SOURCE="$2"
      shift 2
      ;;
    --model-base-url)
      if [ "$#" -lt 2 ]; then
        echo "Missing value for --model-base-url" >&2
        exit 1
      fi
      PRIMARY_BASE_URL="$2"
      shift 2
      ;;
    --model-fallback-base-url)
      if [ "$#" -lt 2 ]; then
        echo "Missing value for --model-fallback-base-url" >&2
        exit 1
      fi
      FALLBACK_BASE_URL="$2"
      shift 2
      ;;
    --auto-download-models)
      if [ "$#" -lt 2 ]; then
        echo "Missing value for --auto-download-models" >&2
        exit 1
      fi
      AUTO_DOWNLOAD_MODELS="$2"
      shift 2
      ;;
    *)
      shift
      ;;
  esac
done

case "${MODEL_SOURCE}" in
  github)
    ;;
  gitee)
    tmp_url="${PRIMARY_BASE_URL}"
    PRIMARY_BASE_URL="${FALLBACK_BASE_URL}"
    FALLBACK_BASE_URL="${tmp_url}"
    ;;
  *)
    echo "Unsupported model source: ${MODEL_SOURCE}" >&2
    exit 1
    ;;
esac

MODEL_FILES=(
  "resnet18_equal_symbol_latest.${PRECISION}.param"
  "resnet18_equal_symbol_latest.${PRECISION}.bin"
  "resnet18_operator_latest.${PRECISION}.param"
  "resnet18_operator_latest.${PRECISION}.bin"
  "resnet34_digit_latest.${PRECISION}.param"
  "resnet34_digit_latest.${PRECISION}.bin"
)

download_one() {
  local base_url="$1"
  local filename="$2"
  local destination="$3"
  local tmp_path="${destination}.tmp"

  rm -f "${tmp_path}"
  if curl -fsSL --retry 3 --connect-timeout 30 -o "${tmp_path}" "${base_url%/}/${filename}"; then
    mv "${tmp_path}" "${destination}"
    return 0
  fi

  rm -f "${tmp_path}"
  return 1
}

mkdir -p "${MODEL_DIR}"

missing_files=()
for filename in "${MODEL_FILES[@]}"; do
  if [ ! -s "${MODEL_DIR}/${filename}" ]; then
    missing_files+=("${filename}")
  fi
done

if [ "${#missing_files[@]}" -eq 0 ]; then
  log "Model bootstrap: all required files already exist in ${MODEL_DIR}"
  exit 0
fi

if [ "${AUTO_DOWNLOAD_MODELS}" != "1" ]; then
  echo "Model bootstrap: missing files found but SHMTU_AUTO_DOWNLOAD_MODELS=${AUTO_DOWNLOAD_MODELS}" >&2
  printf 'Missing files:%s\n' " ${missing_files[*]}" >&2
  exit 1
fi

log "Model bootstrap: begin"
log "  model_dir=${MODEL_DIR}"
log "  precision=${PRECISION}"
log "  model_source=${MODEL_SOURCE}"
log "  primary_base_url=${PRIMARY_BASE_URL}"
log "  fallback_base_url=${FALLBACK_BASE_URL}"
log "  missing_files=${missing_files[*]}"
log "Model bootstrap: downloading missing files into ${MODEL_DIR}"
for filename in "${missing_files[@]}"; do
  log "  downloading ${filename} from primary source"
  if download_one "${PRIMARY_BASE_URL}" "${filename}" "${MODEL_DIR}/${filename}"; then
    continue
  fi
  log "  primary source failed for ${filename}, trying fallback source"
  if download_one "${FALLBACK_BASE_URL}" "${filename}" "${MODEL_DIR}/${filename}"; then
    continue
  fi
  echo "Failed to download ${filename}" >&2
  exit 1
done

for filename in "${MODEL_FILES[@]}"; do
  if [ ! -s "${MODEL_DIR}/${filename}" ]; then
    echo "Model bootstrap: downloaded file missing or empty: ${MODEL_DIR}/${filename}" >&2
    exit 1
  fi
done

log "Model bootstrap: model files are ready in ${MODEL_DIR}"
