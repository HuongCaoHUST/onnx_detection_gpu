#!/usr/bin/env bash
set -euo pipefail

INPUT=${1:-/workspace/test_video.mp4}
MODEL=${2:-/workspace/yolo11n.onnx}
OUTPUT=${3:-}
WIDTH=${VIDEO_WIDTH:-1280}
HEIGHT=${VIDEO_HEIGHT:-720}
FPS=${VIDEO_FPS:-25}
INFER_FPS=${INFER_FPS:-5}
ENGINE=${ENGINE_CACHE:-${MODEL%.*}.nano.fp16.engine}

if [[ ! -f "$INPUT" ]]; then
  echo "Input video does not exist: $INPUT" >&2
  exit 2
fi
if [[ ! -f "$MODEL" ]]; then
  echo "ONNX model does not exist: $MODEL" >&2
  exit 2
fi
if [[ ! -e /dev/nvhost-gpu ]]; then
  echo "Jetson GPU is not visible. Start Docker with: --runtime nvidia" >&2
  exit 3
fi

rm -f "${HOME}/.cache/gstreamer-1.0/registry.aarch64.bin"
gst-inspect-1.0 onnxinference >/dev/null

echo "TensorRT detection: input=$INPUT model=$MODEL ${WIDTH}x${HEIGHT}@${FPS}, inference=${INFER_FPS} FPS"
echo "The first run builds the FP16 engine cache and can take several minutes: $ENGINE"

if gst-inspect-1.0 nvvidconv >/dev/null 2>&1; then
  # decodebin normally selects nvv4l2decoder on Jetson. Its NVMM output must be
  # downloaded to system memory before the custom OpenCV/GStreamer elements.
  DECODE_CONVERT=(nvvidconv ! 'video/x-raw,format=BGRx' ! videoconvert)
else
  # Software decoders already produce system-memory frames.
  export GST_PLUGIN_FEATURE_RANK="nvv4l2decoder:NONE${GST_PLUGIN_FEATURE_RANK:+,$GST_PLUGIN_FEATURE_RANK}"
  DECODE_CONVERT=(videoconvert)
fi

COMMON=(
  filesrc "location=$INPUT" ! decodebin ! "${DECODE_CONVERT[@]}" ! videorate !
  "video/x-raw,framerate=${FPS}/1" ! tee name=t
  t. ! queue max-size-buffers=8 leaky=downstream !
  onnxoverlay name=ov motion-compensation=linear ! videoconvert
)
INFERENCE=(
  t. ! queue max-size-buffers=1 leaky=downstream ! videorate drop-only=true !
  "video/x-raw,framerate=${INFER_FPS}/1" ! videoconvert ! videoscale !
  "video/x-raw,format=RGB,width=640,height=640" !
  onnxinference "model-location=$MODEL" "engine-cache=$ENGINE" !
  onnxpostprocess draw-results=false "original-width=$WIDTH" "original-height=$HEIGHT" !
  onnxtracker tracker-algorithm=bytetrack ! ov.sink_meta
)

if [[ "$OUTPUT" == "display" ]]; then
  echo "Displaying annotated video on HDMI"
  if gst-inspect-1.0 nvvidconv >/dev/null 2>&1 && gst-inspect-1.0 nvoverlaysink >/dev/null 2>&1; then
    gst-launch-1.0 -e "${COMMON[@]}" ! 'video/x-raw,format=I420' ! \
      nvvidconv ! 'video/x-raw(memory:NVMM),format=NV12' ! \
      nvoverlaysink sync=true \
      "${INFERENCE[@]}"
  else
    echo "nvoverlaysink unavailable; falling back to X11 auto video sink" >&2
    gst-launch-1.0 -e "${COMMON[@]}" ! autovideosink sync=true \
      "${INFERENCE[@]}"
  fi
elif [[ -n "$OUTPUT" ]]; then
  if gst-inspect-1.0 nvvidconv >/dev/null 2>&1 && gst-inspect-1.0 nvv4l2h264enc >/dev/null 2>&1; then
    gst-launch-1.0 -e "${COMMON[@]}" ! 'video/x-raw,format=I420' ! \
      nvvidconv ! 'video/x-raw(memory:NVMM),format=NV12' ! \
      nvv4l2h264enc bitrate=4000000 ! h264parse ! matroskamux ! filesink "location=$OUTPUT" \
      "${INFERENCE[@]}"
  else
    echo "nvv4l2h264enc unavailable; using CPU x264 encoder" >&2
    gst-launch-1.0 -e "${COMMON[@]}" ! 'video/x-raw,format=I420' ! \
      x264enc tune=zerolatency speed-preset=ultrafast ! h264parse ! matroskamux ! filesink "location=$OUTPUT" \
      "${INFERENCE[@]}"
  fi
else
  gst-launch-1.0 -e "${COMMON[@]}" ! fpsdisplaysink video-sink=fakesink text-overlay=false sync=false \
    "${INFERENCE[@]}"
fi
