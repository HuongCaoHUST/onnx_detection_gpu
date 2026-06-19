#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

IMAGE=${IMAGE:-onnx-detection-jetson:nano}

for command in meson ninja pkg-config; do
  if ! command -v "$command" >/dev/null 2>&1; then
    echo "Missing host build tool: $command" >&2
    echo "Install prerequisites: sudo apt-get install build-essential meson ninja-build pkg-config libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libopencv-dev" >&2
    exit 2
  fi
done

if [[ ! -f /usr/include/aarch64-linux-gnu/NvInfer.h ]] && [[ ! -f /usr/include/NvInfer.h ]]; then
  echo "TensorRT development headers are missing on the Jetson host (NvInfer.h)." >&2
  echo "Install JetPack development components before continuing." >&2
  exit 2
fi

BUILD_DIR=gst-template/build-jetson
if [[ ! -f "$BUILD_DIR/build.ninja" ]]; then
  meson setup "$BUILD_DIR" gst-template \
    --buildtype=release \
    -Dinference_backend=tensorrt
else
  meson configure "$BUILD_DIR" \
    --buildtype=release \
    -Dinference_backend=tensorrt
fi
ninja -C "$BUILD_DIR"

docker build -f Dockerfile.jetson -t "$IMAGE" .

docker run --rm --runtime nvidia \
  --ipc=host \
  -e VIDEO_WIDTH -e VIDEO_HEIGHT -e VIDEO_FPS -e INFER_FPS -e ENGINE_CACHE \
  -v "$PWD:/workspace" \
  "$IMAGE" "$@"
