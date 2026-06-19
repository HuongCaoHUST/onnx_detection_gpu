#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

IMAGE=${IMAGE:-onnx-detection-jetson:nano}
docker build -f Dockerfile.jetson -t "$IMAGE" .

docker run --rm --runtime nvidia \
  --ipc=host \
  -e VIDEO_WIDTH -e VIDEO_HEIGHT -e VIDEO_FPS -e INFER_FPS -e ENGINE_CACHE \
  -v "$PWD:/workspace" \
  "$IMAGE" "$@"
