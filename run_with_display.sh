#!/bin/bash
# Pipeline GStreamer với YOLO inference - đơn giản, ổn định

export GST_PLUGIN_PATH=/app/gst-template/builddir/gst-plugin
export LD_LIBRARY_PATH=/opt/onnxruntime/lib:$LD_LIBRARY_PATH
export ORT_DISABLE_CUDA=1
export GST_DEBUG=${GST_DEBUG:-1}
export OMP_NUM_THREADS=${OMP_NUM_THREADS:-1}
export OPENBLAS_NUM_THREADS=${OPENBLAS_NUM_THREADS:-1}
export MKL_NUM_THREADS=${MKL_NUM_THREADS:-1}
export OPENCV_FOR_THREADS_NUM=${OPENCV_FOR_THREADS_NUM:-1}

# Tìm X11 display hoạt động
if [ -z "$DISPLAY" ]; then
    if [ -S /tmp/.X11-unix/X0 ]; then
        export DISPLAY=:0
    elif [ -S /tmp/.X11-unix/X1 ]; then
        export DISPLAY=:1
    else
        echo "ERROR: Không tìm thấy X11 display hoạt động"
        echo "Hãy chạy: export DISPLAY=:0 (hoặc :1, :2, ...)"
        exit 1
    fi
fi

echo "Using Display: $DISPLAY"
echo ""

# Clear old registry cache
rm -rf ~/.cache/gstreamer-*

echo "Starting GStreamer YOLO detection pipeline..."
echo "Using:"
echo "  Video: /app/test_video.mp4"
echo "  Model: /app/yolo11n.onnx"
echo "  GStreamer plugins: $GST_PLUGIN_PATH"
echo "  Display: $DISPLAY"
echo ""
echo "Pipeline: Video 1280x720@25fps -> tee -> [Branch1: original], [Branch2: inference]"
echo "          -> onnxoverlay (merge) -> display (1 window)"
echo ""

# Parse arguments
DRAW_RESULTS="${1:-true}"
DURATION="${2:-}"
CLASSIFIER_TYPE="${3:-efficientnet}"

if [ "$CLASSIFIER_TYPE" = "vit" ]; then
    CLASSIFIER_MODEL_1="/app/ppe_vit_small.onnx"
    CLASSIFIER_MODEL_2="/app/ppe_vit_small.onnx"
else
    CLASSIFIER_MODEL_1="/app/ppe_efficientnet_lite0_best.onnx"
    CLASSIFIER_MODEL_2="/app/ppe_efficientnet_lite0_best.onnx"
fi

if [ -n "$DURATION" ]; then
    # Run with timeout
    echo "Running for $DURATION seconds..."
    timeout $DURATION /usr/bin/gst-launch-1.0 -v \
        filesrc location=/app/test_video.mp4 ! decodebin ! videoconvert ! videorate ! video/x-raw,framerate=25/1 ! tee name=t \
        t. ! queue name=display_q max-size-buffers=20 ! onnxoverlay name=ov motion-compensation=false ! videoconvert ! video/x-raw,format=I420 ! x264enc tune=zerolatency ! matroskamux ! filesink location=output_3.mkv sync=true \
        t. ! queue name=infer_q max-size-buffers=20 leaky=no ! videorate drop-only=true ! video/x-raw,framerate=25/2 ! videoscale ! video/x-raw,format=RGB,width=640,height=640 ! onnxinference model-location=/app/yolo11n.onnx ! onnxpostprocess draw-results=false ! onnxtracker tracker-algorithm=sort ! onnxclassifier model-location="$CLASSIFIER_MODEL_1" labels="Helmet,Lamp,Mask,Shoes,Suit" threshold=0.34 ! ov.sink_meta
else
    # Run indefinitely from Webcam
    echo "Running indefinitely using Webcam (close window to stop)..."
    echo "Usage: $0 [draw_results=true|false] [duration_in_seconds] [classifier=efficientnet|vit]"
    echo ""
    timeout 200 /usr/bin/gst-launch-1.0 -v \
        filesrc location="/app/coal_test_video.mp4" ! decodebin ! videoconvert ! videorate ! video/x-raw,framerate=25/1 ! tee name=t \
        t. ! queue name=display_q max-size-buffers=20 ! onnxoverlay name=ov motion-compensation=linear ! videoconvert ! autovideosink sync=true\
        t. ! queue name=infer_q max-size-buffers=1 leaky=downstream ! videorate drop-only=true ! video/x-raw,framerate=25/3 ! videoscale ! video/x-raw,format=RGB,width=640,height=640 ! onnxinference model-location=/app/yolo11n.onnx ! onnxpostprocess conf-threshold=0.3 nms-threshold=0.45 draw-results=false filter-classes="0" ! onnxtracker tracker-algorithm=bytetrack ! onnxclassifier model-location="/app/ppe_efficientnet_lite0_best.onnx" labels="Helmet,Lamp,Mask,Shoes,Suit" threshold=0.34 ! ov.sink_meta
fi
