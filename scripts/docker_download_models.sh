#!/usr/bin/env bash
# ============================================================================
# SHMTU CAS OCR Server — runtime model bootstrap.
#
# Default behavior: download V2 (TriSlot decoder) fp16 mobilenet_v3_small
# weights into ${MODEL_DIR}/v2/  using model-assets.json + SHA256 verification.
#
# For V1 (3-model) weights, pass --include-v1 to also populate ${MODEL_DIR}/v1/.
# V1 still uses the legacy SHA256SUMS.txt-based file list.
#
# Use the SHMTU_MODEL_VERSION=v1 env to switch the runtime server to V1.
# ============================================================================

set -euo pipefail

MODEL_DIR="${SHMTU_MODEL_DIR:-/app/models}"
PRECISION="${SHMTU_PRECISION:-fp16}"
AUTO_DOWNLOAD_MODELS="${SHMTU_AUTO_DOWNLOAD_MODELS:-1}"
MODEL_VERSION="${SHMTU_MODEL_VERSION:-v2}"
MODEL_SOURCE="${SHMTU_MODEL_SOURCE:-github}"
PRIMARY_BASE_URL="${SHMTU_MODEL_BASE_URL:-https://github.com/a645162/shmtu-cas-ocr-model/releases/download}"
PRIMARY_TAG="${SHMTU_MODEL_TAG:-v2.0.2}"
INCLUDE_V1="${SHMTU_INCLUDE_V1:-0}"
INCLUDE_FP32="${SHMTU_INCLUDE_FP32:-0}"

log() {
  printf '[%s] %s\n' "$(date '+%Y-%m-%d %H:%M:%S%z')" "$*"
}

err() {
  printf '[%s] ERROR: %s\n' "$(date '+%Y-%m-%d %H:%M:%S%z')" "$*" >&2
}

V2_DIR="${MODEL_DIR%/}/v2"
V1_DIR="${MODEL_DIR%/}/v1"
V1_BASE_URL="${SHMTU_V1_BASE_URL:-https://github.com/a645162/shmtu-cas-ocr-model/releases/download/v1.0-NCNN}"
V2_BACKBONE="${SHMTU_V2_BACKBONE:-mobilenet_v3_small}"
V2_ENGINE="${SHMTU_V2_ENGINE:-ncnn}"
V2_ASSET_STEM="${SHMTU_V2_ASSET_STEM:-mobilenet_v3_small.trislot_decoder.v2_0}"

# ---- V1 (3-model) ----------------------------------------------------
V1_MODEL_FILES=(
  "resnet18_equal_symbol_latest.${PRECISION}.param"
  "resnet18_equal_symbol_latest.${PRECISION}.bin"
  "resnet18_operator_latest.${PRECISION}.param"
  "resnet18_operator_latest.${PRECISION}.bin"
  "resnet34_digit_latest.${PRECISION}.param"
  "resnet34_digit_latest.${PRECISION}.bin"
)
declare -A V1_EXPECTED_HASHES=()

MAX_DOWNLOAD_ATTEMPTS=3

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

verify_sha256() {
  local filepath="$1"
  local expected_hash="$2"
  local actual_hash
  actual_hash=$(sha256sum "${filepath}" | awk '{print $1}')
  if [ "${actual_hash}" = "${expected_hash}" ]; then
    return 0
  fi
  log "  expected: ${expected_hash}"
  log "  actual:   ${actual_hash}"
  return 1
}

# ---- V1 helper: legacy SHA256SUMS.txt lookup -------------------------
fetch_v1_checksums() {
  local base_url="$1"
  local checksum_tmp="${V1_DIR}/.SHA256SUMS.txt.tmp"
  rm -f "${checksum_tmp}"

  if ! curl -fsSL --retry 3 --connect-timeout 30 -o "${checksum_tmp}" "${base_url%/}/SHA256SUMS.txt"; then
    log "  WARNING: Could not download SHA256SUMS.txt; skipping checksum verification"
    rm -f "${checksum_tmp}"
    return 1
  fi

  while IFS= read -r line || [ -n "${line}" ]; do
    local hash filename
    hash=$(echo "${line}" | awk '{print $1}')
    filename=$(echo "${line}" | awk '{print $2}')
    filename="${filename#\*}"
    if [ -n "${hash}" ] && [ -n "${filename}" ]; then
      V1_EXPECTED_HASHES["${filename}"]="${hash}"
    fi
  done < "${checksum_tmp}"

  rm -f "${checksum_tmp}"
  return 0
}

# ---- V2 helper: model-assets.json lookup -----------------------------
V2_EXPECTED_HASHES=()
V2_FILE_LIST=()

fetch_v2_manifest() {
  local base_url="$1"
  local tag="$2"
  local manifest_tmp="${V2_DIR}/.model-assets.json.tmp"
  rm -f "${manifest_tmp}"

  if ! curl -fsSL --retry 3 --connect-timeout 30 -o "${manifest_tmp}" "${base_url%/}/${tag}/model-assets.json"; then
    err "Could not download model-assets.json from ${base_url%/}/${tag}/"
    rm -f "${manifest_tmp}"
    return 1
  fi

  if ! command -v python3 >/dev/null 2>&1; then
    err "python3 is required to parse model-assets.json"
    rm -f "${manifest_tmp}"
    return 1
  fi

  # Resolve artifacts: engine=ncnn, backbone=V2_BACKBONE, precisions=[PRECISION] (+fp32 if INCLUDE_FP32=1)
  python3 - "$manifest_tmp" "$V2_BACKBONE" "$V2_ENGINE" "$PRECISION" "$INCLUDE_FP32" <<'PYEOF' || {
import json
import sys

with open(sys.argv[1], "r", encoding="utf-8") as fp:
    manifest = json.load(fp)

backbone = sys.argv[2]
engine = sys.argv[3]
primary_precision = sys.argv[4]
include_fp32 = sys.argv[5] == "1"

precisions = {primary_precision}
if include_fp32:
    precisions.add("fp32")

for artifact in manifest.get("artifacts", []):
    if artifact.get("engine") != engine:
        continue
    if artifact.get("backbone") != backbone:
        continue
    precision = artifact.get("precision")
    if precision not in precisions:
        continue
    for entry in artifact.get("files", []):
        name = entry.get("release_asset_name", "")
        sha = entry.get("sha256", "")
        if name and sha:
            print(f"{sha} {name}")
PYEOF
    err "python3 failed to parse manifest"
    rm -f "${manifest_tmp}"
    return 1
  }

  # Parse the python output into the V2 lookup arrays.
  while IFS= read -r line; do
    [ -z "${line}" ] && continue
    local sha name
    sha=$(echo "${line}" | awk '{print $1}')
    name=$(echo "${line}" | awk '{print $2}')
    V2_EXPECTED_HASHES["${name}"]="${sha}"
    V2_FILE_LIST+=("${name}")
  done < <(python3 - "$manifest_tmp" "$V2_BACKBONE" "$V2_ENGINE" "$PRECISION" "$INCLUDE_FP32" <<'PYEOF'
import json
import sys

with open(sys.argv[1], "r", encoding="utf-8") as fp:
    manifest = json.load(fp)

backbone = sys.argv[2]
engine = sys.argv[3]
primary_precision = sys.argv[4]
include_fp32 = sys.argv[5] == "1"

precisions = {primary_precision}
if include_fp32:
    precisions.add("fp32")

for artifact in manifest.get("artifacts", []):
    if artifact.get("engine") != engine:
        continue
    if artifact.get("backbone") != backbone:
        continue
    if artifact.get("precision") not in precisions:
        continue
    for entry in artifact.get("files", []):
        name = entry.get("release_asset_name", "")
        sha = entry.get("sha256", "")
        if name and sha:
            print(f"{sha} {name}")
PYEOF
  )

  rm -f "${manifest_tmp}"
  return 0
}

# ---- Per-version downloaders -----------------------------------------
download_v1() {
  local base_url="$1"
  local missing=()
  mkdir -p "${V1_DIR}"

  for filename in "${V1_MODEL_FILES[@]}"; do
    if [ ! -s "${V1_DIR}/${filename}" ]; then
      missing+=("${filename}")
    fi
  done

  if [ "${#missing[@]}" -eq 0 ]; then
    log "V1: all required files already exist in ${V1_DIR}"
    return 0
  fi

  if [ "${AUTO_DOWNLOAD_MODELS}" != "1" ]; then
    err "V1: missing files but SHMTU_AUTO_DOWNLOAD_MODELS=${AUTO_DOWNLOAD_MODELS}"
    printf '  Missing files:%s\n' " ${missing[*]}" >&2
    return 1
  fi

  log "V1: begin download"
  log "  v1_dir=${V1_DIR}"
  log "  precision=${PRECISION}"
  log "  base_url=${base_url}"
  log "  missing_files=${missing[*]}"

  local has_checksums=false
  if fetch_v1_checksums "${base_url}"; then
    has_checksums=true
    log "  V1 SHA256 checksums loaded, verification enabled"
  fi

  for filename in "${missing[@]}"; do
    local success=false
    for attempt in $(seq 1 "${MAX_DOWNLOAD_ATTEMPTS}"); do
      if [ "${attempt}" -gt 1 ]; then
        log "  retry ${attempt}/${MAX_DOWNLOAD_ATTEMPTS} for ${filename}"
      fi
      log "  downloading ${filename}"
      if download_one "${base_url}" "${filename}" "${V1_DIR}/${filename}"; then
        local expected="${V1_EXPECTED_HASHES[${filename}]:-}"
        if [ "${has_checksums}" = true ] && [ -n "${expected}" ]; then
          if verify_sha256 "${V1_DIR}/${filename}" "${expected}"; then
            log "  SHA256 verified for ${filename}"
            success=true
            break
          else
            log "  SHA256 mismatch for ${filename}, deleting and retrying"
            rm -f "${V1_DIR}/${filename}"
          fi
        else
          success=true
          break
        fi
      fi
    done

    if [ "${success}" = false ]; then
      err "Failed to download V1 ${filename} after ${MAX_DOWNLOAD_ATTEMPTS} attempts"
      return 1
    fi
  done

  for filename in "${V1_MODEL_FILES[@]}"; do
    if [ ! -s "${V1_DIR}/${filename}" ]; then
      err "V1: downloaded file missing or empty: ${V1_DIR}/${filename}"
      return 1
    fi
  done

  log "V1: model files are ready in ${V1_DIR}"
  return 0
}

download_v2() {
  local base_url="$1"
  local tag="$2"
  local missing=()
  mkdir -p "${V2_DIR}"

  # Determine which files to expect from the manifest.
  V2_FILE_LIST=()
  if ! fetch_v2_manifest "${base_url}" "${tag}"; then
    return 1
  fi

  if [ "${#V2_FILE_LIST[@]}" -eq 0 ]; then
    err "V2: manifest yielded no matching artifacts (backbone=${V2_BACKBONE}, engine=${V2_ENGINE}, precisions=${PRECISION}$([ "${INCLUDE_FP32}" = "1" ] && echo ",fp32" || echo ""))"
    return 1
  fi

  for filename in "${V2_FILE_LIST[@]}"; do
    if [ ! -s "${V2_DIR}/${filename}" ]; then
      missing+=("${filename}")
    fi
  done

  if [ "${#missing[@]}" -eq 0 ]; then
    log "V2: all required files already exist in ${V2_DIR}"
    return 0
  fi

  if [ "${AUTO_DOWNLOAD_MODELS}" != "1" ]; then
    err "V2: missing files but SHMTU_AUTO_DOWNLOAD_MODELS=${AUTO_DOWNLOAD_MODELS}"
    printf '  Missing files:%s\n' " ${missing[*]}" >&2
    return 1
  fi

  log "V2: begin download"
  log "  v2_dir=${V2_DIR}"
  log "  precision=${PRECISION}"
  log "  include_fp32=${INCLUDE_FP32}"
  log "  backbone=${V2_BACKBONE}"
  log "  asset_stem=${V2_ASSET_STEM}"
  log "  base_url=${base_url}"
  log "  tag=${tag}"
  log "  missing_files=${missing[*]}"

  for filename in "${missing[@]}"; do
    local success=false
    for attempt in $(seq 1 "${MAX_DOWNLOAD_ATTEMPTS}"); do
      if [ "${attempt}" -gt 1 ]; then
        log "  retry ${attempt}/${MAX_DOWNLOAD_ATTEMPTS} for ${filename}"
      fi
      log "  downloading ${filename}"
      if download_one "${base_url%/}/${tag}" "${filename}" "${V2_DIR}/${filename}"; then
        local expected="${V2_EXPECTED_HASHES[${filename}]:-}"
        if [ -n "${expected}" ]; then
          if verify_sha256 "${V2_DIR}/${filename}" "${expected}"; then
            log "  SHA256 verified for ${filename}"
            success=true
            break
          else
            log "  SHA256 mismatch for ${filename}, deleting and retrying"
            rm -f "${V2_DIR}/${filename}"
          fi
        else
          success=true
          break
        fi
      fi
    done

    if [ "${success}" = false ]; then
      err "Failed to download V2 ${filename} after ${MAX_DOWNLOAD_ATTEMPTS} attempts"
      return 1
    fi
  done

  for filename in "${V2_FILE_LIST[@]}"; do
    if [ ! -s "${V2_DIR}/${filename}" ]; then
      err "V2: downloaded file missing or empty: ${V2_DIR}/${filename}"
      return 1
    fi
  done

  log "V2: model files are ready in ${V2_DIR}"
  return 0
}

mkdir -p "${MODEL_DIR}"

# Decide which version(s) to download.
need_v1=0
need_v2=0
case "${MODEL_VERSION}" in
  v1)
    need_v1=1
    ;;
  v2)
    need_v2=1
    ;;
  *)
    err "Unsupported SHMTU_MODEL_VERSION=${MODEL_VERSION} (expected: v1 or v2)"
    exit 1
    ;;
esac
if [ "${INCLUDE_V1}" = "1" ]; then
  need_v1=1
fi

log "Model bootstrap: begin"
log "  model_dir=${MODEL_DIR}"
log "  model_version=${MODEL_VERSION}"
log "  precision=${PRECISION}"
log "  include_v1=${INCLUDE_V1}"
log "  include_fp32=${INCLUDE_FP32}"
log "  auto_download_models=${AUTO_DOWNLOAD_MODELS}"

if [ "${need_v2}" = "1" ]; then
  download_v2 "${PRIMARY_BASE_URL}" "${PRIMARY_TAG}"
fi
if [ "${need_v1}" = "1" ]; then
  download_v1 "${V1_BASE_URL}"
fi

log "Model bootstrap: complete"
