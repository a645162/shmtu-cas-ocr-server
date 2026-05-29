#!/usr/bin/env bash

set -euo pipefail

VARIANT="${1:-}"

if [ -z "${VARIANT}" ]; then
  echo "Usage: $0 [cpu|vulkan]" >&2
  exit 1
fi

case "${VARIANT}" in
  cpu)
    BASE_IMAGE="shmtu-cas-ocr-server:cpu"
    BUNDLED_IMAGE="shmtu-cas-ocr-server:cpu-bundled"
    ;;
  vulkan)
    BASE_IMAGE="shmtu-cas-ocr-server:vulkan"
    BUNDLED_IMAGE="shmtu-cas-ocr-server:vulkan-bundled"
    ;;
  *)
    echo "Unsupported variant: ${VARIANT}" >&2
    exit 1
    ;;
esac

MODEL_FILES=(
  /app/models/resnet18_operator_latest.fp16.bin
  /app/models/resnet18_operator_latest.fp16.param
  /app/models/resnet18_equal_symbol_latest.fp16.bin
  /app/models/resnet18_equal_symbol_latest.fp16.param
  /app/models/resnet34_digit_latest.fp16.bin
  /app/models/resnet34_digit_latest.fp16.param
)

base_id="$(docker image inspect --format '{{.Id}}' "${BASE_IMAGE}")"
bundled_id="$(docker image inspect --format '{{.Id}}' "${BUNDLED_IMAGE}")"

echo "Base image:    ${BASE_IMAGE} (${base_id})"
echo "Bundled image: ${BUNDLED_IMAGE} (${bundled_id})"

if [ "${base_id}" = "${bundled_id}" ]; then
  echo "Bundled image unexpectedly matches base image ID." >&2
  exit 1
fi

for model_file in "${MODEL_FILES[@]}"; do
  docker run --rm --entrypoint /bin/sh "${BASE_IMAGE}" -lc "! test -f '${model_file}'"
  docker run --rm --entrypoint /bin/sh "${BUNDLED_IMAGE}" -lc "test -s '${model_file}'"
done

echo "Runtime image verification passed for ${VARIANT}."
