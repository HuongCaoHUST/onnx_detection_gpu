#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/base.h>
#include <gst/video/video.h>

#include "gstonnxpostprocess.h"
#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include <iostream>

GST_DEBUG_CATEGORY_STATIC (gst_onnxpostprocess_debug);
#define GST_CAT_DEFAULT gst_onnxpostprocess_debug

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, format=(string)RGB, width=(int)640, height=(int)640")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, format=(string)RGB, width=(int)640, height=(int)640")
    );

#define gst_onnxpostprocess_parent_class parent_class
G_DEFINE_TYPE (Gstonnxpostprocess, gst_onnxpostprocess, GST_TYPE_BASE_TRANSFORM);
GST_ELEMENT_REGISTER_DEFINE (onnxpostprocess, "onnxpostprocess", GST_RANK_NONE,
    GST_TYPE_ONNXPOSTPROCESS);

static GstFlowReturn gst_onnxpostprocess_transform_ip (GstBaseTransform * base, GstBuffer * outbuf);

static void
gst_onnxpostprocess_class_init (GstonnxpostprocessClass * klass)
{
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBaseTransformClass *basetransform_class = (GstBaseTransformClass *) klass;

  gst_element_class_set_details_simple (gstelement_class,
      "ONNX Postprocess", "Filter/Video",
      "Performs bounding box drawing based on YOLO output tensor", "HuongCao <<user@hostname.org>>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));

  basetransform_class->transform_ip = GST_DEBUG_FUNCPTR (gst_onnxpostprocess_transform_ip);

  GST_DEBUG_CATEGORY_INIT (gst_onnxpostprocess_debug, "onnxpostprocess", 0, "onnxpostprocess element");
}

static void
gst_onnxpostprocess_init (Gstonnxpostprocess * filter)
{
}

const int INPUT_WIDTH = 640;
const int INPUT_HEIGHT = 640;
const float CONFIDENCE_THRESHOLD = 0.45f;
const float NMS_THRESHOLD = 0.45f;

const std::vector<std::string> CLASS_NAMES = {
    "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat", "traffic light",
    "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat", "dog", "horse", "sheep", "cow",
    "elephant", "bear", "zebra", "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee",
    "skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove", "skateboard", "surfboard", "tennis racket", "bottle",
    "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple", "sandwich", "orange",
    "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch", "potted plant", "bed",
    "dining table", "toilet", "tv", "laptop", "mouse", "remote", "keyboard", "cell phone", "microwave", "oven",
    "toaster", "sink", "refrigerator", "book", "clock", "vase", "scissors", "teddy bear", "hair drier", "toothbrush"
};

static GstFlowReturn
gst_onnxpostprocess_transform_ip (GstBaseTransform * base, GstBuffer * outbuf)
{
  if (gst_buffer_n_memory(outbuf) < 2) {
    // Buffer doesn't contain side-loaded tensor, just pass it forward
    return GST_FLOW_OK;
  }

  GstMemory *image_mem = gst_buffer_peek_memory (outbuf, 0);
  // Assuming the appended memory chunk is at the end
  guint n_mem = gst_buffer_n_memory(outbuf);
  GstMemory *tensor_mem = gst_buffer_peek_memory (outbuf, n_mem - 1);

  GstMapInfo img_map;
  if (!gst_memory_map (image_mem, &img_map, GST_MAP_READWRITE)) {
      GST_ERROR_OBJECT (base, "Failed to map image memory");
      return GST_FLOW_ERROR;
  }

  GstMapInfo tensor_map;
  if (!gst_memory_map (tensor_mem, &tensor_map, GST_MAP_READ)) {
      GST_ERROR_OBJECT (base, "Failed to map tensor memory");
      gst_memory_unmap (image_mem, &img_map);
      return GST_FLOW_ERROR;
  }

  float* output_data = (float*)tensor_map.data;
  int num_classes = 80;
  int num_boxes = 8400;

  std::vector<cv::Rect> boxes;
  std::vector<float> confidences;
  std::vector<int> class_ids;

  // Since fixed input 640x640 is set, scale is 1.0. 
  // However we can use the same logic mapping back to 640x640 frame.
  float x_scale = 1.0f;
  float y_scale = 1.0f;

  for (int i = 0; i < num_boxes; ++i) {
      float max_conf = 0.0f;
      int best_class_id = -1;
      
      for (int c = 0; c < num_classes; ++c) {
          float conf = output_data[(4 + c) * num_boxes + i];
          if (conf > max_conf) {
              max_conf = conf;
              best_class_id = c;
          }
      }

      if (max_conf > CONFIDENCE_THRESHOLD) {
          float cx = output_data[0 * num_boxes + i];
          float cy = output_data[1 * num_boxes + i];
          float w = output_data[2 * num_boxes + i];
          float h = output_data[3 * num_boxes + i];

          int left = static_cast<int>((cx - w / 2) * x_scale);
          int top  = static_cast<int>((cy - h / 2) * y_scale);
          int width = static_cast<int>(w * x_scale);
          int height = static_cast<int>(h * y_scale);

          boxes.push_back(cv::Rect(left, top, width, height));
          confidences.push_back(max_conf);
          class_ids.push_back(best_class_id);
      }
  }

  std::vector<int> indices;
  cv::dnn::NMSBoxes(boxes, confidences, CONFIDENCE_THRESHOLD, NMS_THRESHOLD, indices);

  GST_INFO_OBJECT (base, "Detected %zu objects", indices.size());

  cv::Mat frame(INPUT_HEIGHT, INPUT_WIDTH, CV_8UC3, img_map.data);

  for (int idx : indices) {
      cv::Rect box = boxes[idx];
      int class_id = class_ids[idx];
      float conf = confidences[idx];

      cv::rectangle(frame, box, cv::Scalar(0, 255, 0), 2);
      std::string label = CLASS_NAMES[class_id] + " " + cv::format("%.2f", conf);
      
      int baseLine;
      cv::Size labelSize = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseLine);
      cv::rectangle(frame, cv::Point(box.x, box.y - labelSize.height),
                    cv::Point(box.x + labelSize.width, box.y + baseLine), cv::Scalar(0, 255, 0), cv::FILLED);
      cv::putText(frame, label, cv::Point(box.x, box.y), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0), 1);
  }

  gst_memory_unmap (tensor_mem, &tensor_map);
  gst_memory_unmap (image_mem, &img_map);

  gst_buffer_remove_memory (outbuf, n_mem - 1);

  return GST_FLOW_OK;
}

static gboolean
onnxpostprocess_init (GstPlugin * onnxpostprocess)
{
  return GST_ELEMENT_REGISTER (onnxpostprocess, onnxpostprocess);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    onnxpostprocess,
    "YOLO ONNX Postprocess filter",
    onnxpostprocess_init,
    PACKAGE_VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
