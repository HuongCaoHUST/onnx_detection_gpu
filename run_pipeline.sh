#!/bin/bash
# Pipeline GStreamer với YOLO inference

export GST_PLUGIN_PATH=/home/huongcao/onnx_detection_gpu/gst-template/builddir/gst-plugin
export LD_LIBRARY_PATH=/opt/onnxruntime/lib:$LD_LIBRARY_PATH

# Clear old registry cache để GStreamer reload plugins
rm -rf ~/.cache/gstreamer-*

echo "Starting GStreamer YOLO detection pipeline..."
echo "Using:"
echo "  Video: /home/huongcao/onnx_detection_gpu/test_video.mp4"
echo "  Model: /home/huongcao/onnx_detection_gpu/yolo11n.onnx"
echo "  GStreamer plugins: $GST_PLUGIN_PATH"
echo ""

# Parse arguments
OUTPUT_FILE="${1:-}"
DRAW_RESULTS="${2:-true}"

if [ -n "$OUTPUT_FILE" ]; then
    echo "Saving output to: $OUTPUT_FILE"
    echo "Pipeline: Video -> tee -> [Branch1: save], [Branch2: inference -> overlay metadata]"
    # Run pipeline with 2 branches (original + detection) and save to file
    /usr/bin/gst-launch-1.0 \
        filesrc location=/home/huongcao/onnx_detection_gpu/test_video.mp4 ! decodebin ! videoconvert ! videorate ! tee name=t \
        t. ! queue max-size-buffers=5 ! onnxoverlay name=ov ! videoconvert ! x264enc speed-preset=ultrafast ! h264parse ! mp4mux ! filesink location=$OUTPUT_FILE \
        t. ! queue max-size-buffers=2 ! videoscale ! video/x-raw,format=RGB,width=640,height=640 ! videorate ! onnxinference model-location=/home/huongcao/onnx_detection_gpu/yolo11n.onnx ! onnxpostprocess draw-results=$DRAW_RESULTS ! ov.sink_meta
else
    echo "No output file specified. Streaming to display..."
    echo "Usage: $0 <output_file> [draw_results=true|false]"
    echo ""
    echo "Pipeline: Video -> tee -> [Branch1: display], [Branch2: inference -> overlay metadata]"
    echo "If detection is slow, video still plays smoothly (no metadata overlay)"
    echo ""
    # Run pipeline with 2 branches (original + detection) and display
    /usr/bin/gst-launch-1.0 \
        filesrc location=/home/huongcao/onnx_detection_gpu/test_video.mp4 ! decodebin ! videoconvert ! videorate ! tee name=t \
        t. ! queue max-size-buffers=5 ! onnxoverlay name=ov ! videoconvert ! fpsdisplaysink sync=true \
        t. ! queue max-size-buffers=2 ! videoscale ! video/x-raw,format=RGB,width=640,height=640 ! videorate ! onnxinference model-location=/home/huongcao/onnx_detection_gpu/yolo11n.onnx ! onnxpostprocess draw-results=$DRAW_RESULTS ! ov.sink_meta
fi

