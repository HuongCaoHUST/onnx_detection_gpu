#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR=/workspace/gst-template/build-compose
PLUGIN="$BUILD_DIR/gst-plugin/libgstonnxinference.so"

if [[ ! -f /usr/include/aarch64-linux-gnu/NvInfer.h ]] &&
   [[ ! -f /usr/include/NvInfer.h ]]; then
  echo "TensorRT headers are not visible. Check the host include bind mount in compose.yaml." >&2
  exit 2
fi

if [[ ! -f "$BUILD_DIR/build.ninja" ]]; then
  echo "Building the Jetson TensorRT GStreamer plugin (first start only)..."
  if [[ -d "$BUILD_DIR" ]]; then
    rm -rf "$BUILD_DIR"
  fi
  meson setup "$BUILD_DIR" /workspace/gst-template \
    --buildtype=release \
    -Dinference_backend=tensorrt
fi
ninja -C "$BUILD_DIR"

if [[ ! -f "$PLUGIN" ]]; then
  echo "Plugin build completed but $PLUGIN was not created." >&2
  exit 2
fi

exec /usr/local/bin/run-jetson-detection "$@"
