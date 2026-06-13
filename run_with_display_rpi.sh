#!/bin/bash
# Raspberry Pi pipeline: keep the display live by dropping old frames and
# running the expensive ONNX path at a lower rate.

set -e

export GST_PLUGIN_PATH=/app/gst-template/builddir/gst-plugin
export LD_LIBRARY_PATH=/opt/onnxruntime/lib:$LD_LIBRARY_PATH
export ORT_DISABLE_CUDA=1
export GST_DEBUG=${GST_DEBUG:-0}
export OMP_NUM_THREADS=${OMP_NUM_THREADS:-1}
export OPENBLAS_NUM_THREADS=${OPENBLAS_NUM_THREADS:-1}
export MKL_NUM_THREADS=${MKL_NUM_THREADS:-1}
export OPENCV_FOR_THREADS_NUM=${OPENCV_FOR_THREADS_NUM:-1}
export ORT_INTRA_THREADS=${ORT_INTRA_THREADS:-2}
export ORT_INTER_THREADS=${ORT_INTER_THREADS:-1}

VIDEO_SINK="${VIDEO_SINK:-kmssink}"

if [ "$VIDEO_SINK" != "kmssink" ] && [ -z "$DISPLAY" ]; then
    if [ -S /tmp/.X11-unix/X0 ]; then
        export DISPLAY=:0
    elif [ -S /tmp/.X11-unix/X1 ]; then
        export DISPLAY=:1
    else
        echo "ERROR: Không tìm thấy X11 display hoạt động"
        echo "Hãy chạy: export DISPLAY=:0"
        exit 1
    fi
fi

rm -rf ~/.cache/gstreamer-*

VIDEO_PATH="${VIDEO_PATH:-/videos/input.mp4}"
if [ ! -f "$VIDEO_PATH" ]; then
    VIDEO_PATH="/app/coal_test_video.mp4"
fi
MODEL_PATH="${MODEL_PATH:-/app/yolo11n.onnx}"
CLASSIFIER_MODEL="${CLASSIFIER_MODEL:-/app/ppe_efficientnet_lite0_best.onnx}"
DISPLAY_FPS="${DISPLAY_FPS:-15}"
INFER_FPS_NUM="${INFER_FPS_NUM:-3}"
INFER_FPS_DEN="${INFER_FPS_DEN:-1}"
CONF_THRESHOLD="${CONF_THRESHOLD:-0.45}"
NMS_THRESHOLD="${NMS_THRESHOLD:-0.45}"
ENABLE_CLASSIFIER="${ENABLE_CLASSIFIER:-0}"
USE_TRACKER="${USE_TRACKER:-1}"
MOTION_COMPENSATION="${MOTION_COMPENSATION:-linear}"
FILTER_CLASSES="${FILTER_CLASSES:-}"
DRAW_LABELS="${DRAW_LABELS:-false}"
PRINT_CONFIG="${PRINT_CONFIG:-0}"

if [ "$PRINT_CONFIG" = "1" ]; then
    echo "Starting Raspberry Pi optimized GStreamer pipeline..."
    echo "  Video: $VIDEO_PATH"
    echo "  YOLO model: $MODEL_PATH"
    echo "  Display FPS cap: ${DISPLAY_FPS}/1"
    echo "  Inference FPS cap: ${INFER_FPS_NUM}/${INFER_FPS_DEN}"
    echo "  Confidence threshold: $CONF_THRESHOLD"
    echo "  Tracker: $USE_TRACKER"
    echo "  Classifier: $ENABLE_CLASSIFIER"
    echo "  Motion compensation: $MOTION_COMPENSATION"
    echo "  Draw labels: $DRAW_LABELS"
    echo "  Filter classes: ${FILTER_CLASSES:-all}"
    echo "  Sink: $VIDEO_SINK"
    echo ""
fi

META_CHAIN="onnxpostprocess conf-threshold=${CONF_THRESHOLD} nms-threshold=${NMS_THRESHOLD} draw-results=false"
if [ -n "$FILTER_CLASSES" ]; then
    META_CHAIN="$META_CHAIN filter-classes=\"$FILTER_CLASSES\""
fi

if [ "$USE_TRACKER" = "1" ]; then
    META_CHAIN="$META_CHAIN ! onnxtracker tracker-algorithm=sort"
fi

if [ "$ENABLE_CLASSIFIER" = "1" ]; then
    META_CHAIN="$META_CHAIN ! onnxclassifier model-location=\"$CLASSIFIER_MODEL\" labels=\"Helmet,Lamp,Mask,Shoes,Suit\" threshold=0.45"
fi

eval /usr/bin/gst-launch-1.0 -q \
    filesrc location="\"$VIDEO_PATH\"" ! decodebin ! videoconvert ! videorate ! video/x-raw,framerate=${DISPLAY_FPS}/1 ! tee name=t \
    t. ! queue name=display_q max-size-buffers=2 leaky=downstream ! onnxoverlay name=ov motion-compensation=${MOTION_COMPENSATION} draw-labels=${DRAW_LABELS} ! videoconvert ! fpsdisplaysink video-sink="\"$VIDEO_SINK\"" text-overlay=false sync=false silent=true \
    t. ! queue name=infer_q max-size-buffers=1 leaky=downstream ! videorate drop-only=true ! video/x-raw,framerate=${INFER_FPS_NUM}/${INFER_FPS_DEN} ! videoscale method=nearest-neighbour ! video/x-raw,format=RGB,width=640,height=640 ! onnxinference model-location="\"$MODEL_PATH\"" ! $META_CHAIN ! ov.sink_meta
