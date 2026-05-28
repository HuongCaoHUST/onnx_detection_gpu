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
    timeout $DURATION /usr/bin/gst-launch-1.0 -v \
        filesrc location=/home/huongcao/onnx_detection_gpu/test_video.mp4 ! decodebin ! videoconvert ! videorate ! video/x-raw,framerate=25/1 ! tee name=t \
        t. ! queue max-size-buffers=20 ! onnxoverlay name=ov motion-compensation=false ! videoconvert ! video/x-raw,format=I420 ! x264enc tune=zerolatency ! matroskamux ! filesink location=output_3.mkv sync=true \
        t. ! queue max-size-buffers=20 leaky=no ! videorate drop-only=true ! video/x-raw,framerate=25/2 ! videoscale ! video/x-raw,format=RGB,width=640,height=640 ! onnxinference model-location=/home/huongcao/onnx_detection_gpu/yolo11n.onnx ! onnxpostprocess draw-results=false ! onnxtracker tracker-algorithm=sort ! onnxclassifier model-location=/home/huongcao/onnx_detection_gpu/ppe_efficientnet_lite0.onnx labels="Helmet,Lamp,Mask,Shoes,Suit" threshold=0.34 ! ov.sink_meta
else
    # Run indefinitely from Webcam
    echo "Running indefinitely using Webcam (close window to stop)..."
    echo "Usage: $0 [draw_results=true|false] [duration_in_seconds]"
    echo ""
    timeout 200 /usr/bin/gst-launch-1.0 -v \
        filesrc location=./coal_test_video.mp4 ! decodebin ! videoconvert ! videorate ! video/x-raw,framerate=25/1 ! tee name=t \
        t. ! queue max-size-buffers=10 ! onnxoverlay name=ov motion-compensation=linear ! videoconvert ! video/x-raw,format=I420 ! x264enc tune=zerolatency ! matroskamux ! filesink location=output_new_6.mkv sync=true \
        t. ! queue max-size-buffers=2 leaky=downstream ! videorate drop-only=true ! video/x-raw,framerate=25/2 ! videoscale ! video/x-raw,format=RGB,width=640,height=640 ! onnxinference model-location=yolo11n.onnx ! onnxpostprocess conf-threshold=0.3 nms-threshold=0.45 draw-results=false filter-classes="0" ! onnxtracker tracker-algorithm=sort ! onnxclassifier model-location=ppe_efficientnet_lite0_best.onnx labels="Helmet,Lamp,Mask,Shoes,Suit" threshold=0.34 ! ov.sink_meta
fi
