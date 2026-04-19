#!/bin/bash
# Ghi output của pipeline YOLO detection ra file MKV (1 phút)

export GST_PLUGIN_PATH=/home/huongcao/onnx_detection_gpu/gst-template/builddir/gst-plugin
export LD_LIBRARY_PATH=/opt/onnxruntime/lib:$LD_LIBRARY_PATH
export ORT_DISABLE_CUDA=1
# Giảm log để không ảnh hưởng performance khi ghi video
export GST_DEBUG=onnxoverlay:2,onnxpostprocess:2,onnxinference:2

DURATION=10  # 1 phút
OUTPUT_FILE="${1:-/home/huongcao/onnx_detection_gpu/output_$(date +%Y%m%d_%H%M%S).mkv}"

echo "============================================"
echo "  YOLO Detection Pipeline - Record to MKV  "
echo "============================================"
echo "  Input  : /home/huongcao/onnx_detection_gpu/test_video.mp4"
echo "  Model  : /home/huongcao/onnx_detection_gpu/yolo11n.onnx"
echo "  Output : $OUTPUT_FILE"
echo "  Duration: ${DURATION}s (1 minute)"
echo "============================================"
echo ""

# Clear old registry cache
rm -rf ~/.cache/gstreamer-*

echo "Starting recording..."

timeout $DURATION /usr/bin/gst-launch-1.0 -e \
    filesrc location=/home/huongcao/onnx_detection_gpu/test_video.mp4 ! decodebin ! videoconvert ! videorate ! video/x-raw,framerate=25/1 ! tee name=t \
    t. ! queue max-size-buffers=10 ! onnxoverlay name=ov ! videoconvert ! \
        x264enc tune=zerolatency bitrate=4000 speed-preset=fast ! \
        matroskamux ! \
        filesink location="$OUTPUT_FILE" \
    t. ! queue max-size-buffers=1 leaky=downstream ! videoscale ! \
        video/x-raw,format=RGB,width=640,height=640 ! \
        onnxinference model-location=/home/huongcao/onnx_detection_gpu/yolo11n.onnx ! \
        onnxpostprocess draw-results=false ! ov.sink_meta

EXIT_CODE=$?

if [ $EXIT_CODE -eq 0 ] || [ $EXIT_CODE -eq 124 ]; then
    echo ""
    echo "✅ Recording completed!"
    echo "   Saved to: $OUTPUT_FILE"
    if [ -f "$OUTPUT_FILE" ]; then
        SIZE=$(du -h "$OUTPUT_FILE" | cut -f1)
        echo "   File size: $SIZE"
    fi
else
    echo ""
    echo "❌ Pipeline exited with error code: $EXIT_CODE"
fi
