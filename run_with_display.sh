#!/bin/bash
# Pipeline GStreamer với YOLO inference - đơn giản, ổn định

export GST_PLUGIN_PATH=/home/huongcao/onnx_detection_gpu/gst-template/builddir/gst-plugin
export LD_LIBRARY_PATH=/opt/onnxruntime/lib:$LD_LIBRARY_PATH
export ORT_DISABLE_CUDA=1
export GST_DEBUG=onnxoverlay:5,onnxpostprocess:5,onnxinference:5

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
echo "  Video: /home/huongcao/onnx_detection_gpu/test_video.mp4"
echo "  Model: /home/huongcao/onnx_detection_gpu/yolo11n.onnx"
echo "  GStreamer plugins: $GST_PLUGIN_PATH"
echo "  Display: $DISPLAY"
echo ""
echo "Pipeline: Video 1280x720@25fps -> tee -> [Branch1: original], [Branch2: inference]"
echo "          -> onnxoverlay (merge) -> display (1 window)"
echo ""

# Parse arguments
DRAW_RESULTS="${1:-true}"
DURATION="${2:-}"

if [ -n "$DURATION" ]; then
    # Run with timeout
    echo "Running for $DURATION seconds..."
    timeout $DURATION /usr/bin/gst-launch-1.0 \
        filesrc location=/home/huongcao/onnx_detection_gpu/test_video.mp4 ! decodebin ! videoconvert ! videorate ! tee name=t \
        t. ! queue max-size-buffers=10 ! onnxoverlay name=ov ! videoconvert ! fpsdisplaysink sync=false \
        t. ! queue max-size-buffers=3 leaky=downstream ! videoscale ! video/x-raw,format=RGB,width=640,height=640 ! videorate ! onnxinference model-location=/home/huongcao/onnx_detection_gpu/yolo11n.onnx ! onnxpostprocess draw-results=false ! ov.sink_meta
else
    # Run indefinitely
    echo "Running indefinitely (close window to stop)..."
    echo "Usage: $0 [draw_results=true|false] [duration_in_seconds]"
    echo ""
    /usr/bin/gst-launch-1.0 \
        filesrc location=/home/huongcao/onnx_detection_gpu/test_video.mp4 ! decodebin ! videoconvert ! videorate ! tee name=t \
        t. ! queue max-size-buffers=10 ! onnxoverlay name=ov ! videoconvert ! fpsdisplaysink sync=false \
        t. ! queue max-size-buffers=3 leaky=downstream ! videoscale ! video/x-raw,format=RGB,width=640,height=640 ! videorate ! onnxinference model-location=/home/huongcao/onnx_detection_gpu/yolo11n.onnx ! onnxpostprocess draw-results=false ! ov.sink_meta
fi


