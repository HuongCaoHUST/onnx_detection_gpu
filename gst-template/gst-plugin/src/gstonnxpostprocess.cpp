#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/base.h>

#include "gstonnxmeta.h"
#include "gstonnxpostprocess.h"
#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include <iostream>

enum
{
  PROP_0,
  PROP_DRAW_RESULTS,
  PROP_ORIGINAL_WIDTH,
  PROP_ORIGINAL_HEIGHT,
  PROP_CONF_THRESHOLD,
  PROP_NMS_THRESHOLD,
  PROP_FILTER_CLASSES,
};

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

static GstFlowReturn gst_onnxpostprocess_transform (GstBaseTransform * base, GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_onnxpostprocess_transform_size (GstBaseTransform * base, GstPadDirection direction, GstCaps * caps, gsize size, GstCaps * othercaps, gsize * othersize);
static void gst_onnxpostprocess_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_onnxpostprocess_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_onnxpostprocess_finalize (GObject * object);

static void
gst_onnxpostprocess_class_init (GstonnxpostprocessClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBaseTransformClass *basetransform_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = gst_onnxpostprocess_set_property;
  gobject_class->get_property = gst_onnxpostprocess_get_property;
  gobject_class->finalize = gst_onnxpostprocess_finalize;

  g_object_class_install_property (gobject_class, PROP_DRAW_RESULTS,
      g_param_spec_boolean ("draw-results", "Draw Results",
          "Whether to draw the detection results on the frame", TRUE,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_ORIGINAL_WIDTH,
      g_param_spec_int ("original-width", "Original Width",
          "Original video width before resize to 640x640", 0, G_MAXINT, 0,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_ORIGINAL_HEIGHT,
      g_param_spec_int ("original-height", "Original Height",
          "Original video height before resize to 640x640", 0, G_MAXINT, 0,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_CONF_THRESHOLD,
      g_param_spec_float ("conf-threshold", "Confidence Threshold",
          "Confidence threshold for object detection", 0.0, 1.0, 0.45,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_NMS_THRESHOLD,
      g_param_spec_float ("nms-threshold", "NMS Threshold",
          "Non-maximum suppression threshold", 0.0, 1.0, 0.45,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_FILTER_CLASSES,
      g_param_spec_string ("filter-classes", "Filter Classes",
          "Comma-separated list of class IDs to filter (e.g. '0,2'). Empty means no filter.", "",
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_details_simple (gstelement_class,
      "ONNX Postprocess", "Filter/Video",
      "Performs bounding box drawing based on YOLO output tensor", "HuongCao <<user@hostname.org>>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));

  basetransform_class->transform_size = GST_DEBUG_FUNCPTR (gst_onnxpostprocess_transform_size);
  basetransform_class->transform = GST_DEBUG_FUNCPTR (gst_onnxpostprocess_transform);

  GST_DEBUG_CATEGORY_INIT (gst_onnxpostprocess_debug, "onnxpostprocess", 0, "onnxpostprocess element");
}

static void
gst_onnxpostprocess_init (Gstonnxpostprocess * filter)
{
  filter->draw_results = TRUE;
  filter->original_width = 640;  /* Default to 640x640 (no scaling) */
  filter->original_height = 640;
  filter->conf_threshold = 0.45f;
  filter->nms_threshold = 0.45f;
  filter->filter_classes = NULL;
  filter->filter_ids = NULL;
  filter->num_filter_ids = 0;
}

static void
gst_onnxpostprocess_finalize (GObject * object)
{
  Gstonnxpostprocess *filter = GST_ONNXPOSTPROCESS (object);
  g_free (filter->filter_classes);
  g_free (filter->filter_ids);
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_onnxpostprocess_transform_size (GstBaseTransform * base, GstPadDirection direction, GstCaps * caps, gsize size, GstCaps * othercaps, gsize * othersize)
{
  // Output size is same as input (only image, tensor metadata is in separate memory)
  *othersize = size;
  return TRUE;
}

static void
gst_onnxpostprocess_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  Gstonnxpostprocess *filter = GST_ONNXPOSTPROCESS (object);

  switch (prop_id) {
    case PROP_DRAW_RESULTS:
      filter->draw_results = g_value_get_boolean (value);
      break;
    case PROP_ORIGINAL_WIDTH:
      filter->original_width = g_value_get_int (value);
      break;
    case PROP_ORIGINAL_HEIGHT:
      filter->original_height = g_value_get_int (value);
      break;
    case PROP_CONF_THRESHOLD:
      filter->conf_threshold = g_value_get_float (value);
      break;
    case PROP_NMS_THRESHOLD:
      filter->nms_threshold = g_value_get_float (value);
      break;
    case PROP_FILTER_CLASSES:
      g_free (filter->filter_classes);
      filter->filter_classes = g_value_dup_string (value);
      g_free (filter->filter_ids);
      filter->filter_ids = NULL;
      filter->num_filter_ids = 0;
      if (filter->filter_classes && strlen (filter->filter_classes) > 0) {
        gchar **split = g_strsplit (filter->filter_classes, ",", -1);
        guint count = g_strv_length (split);
        if (count > 0) {
          filter->filter_ids = g_new0 (gint, count);
          for (guint i = 0; i < count; i++) {
            gchar *endptr = NULL;
            gchar *str = g_strstrip (split[i]);
            if (strlen(str) > 0) {
              gint id = strtol (str, &endptr, 10);
              if (endptr != str) {
                filter->filter_ids[filter->num_filter_ids++] = id;
              }
            }
          }
        }
        g_strfreev (split);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_onnxpostprocess_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  Gstonnxpostprocess *filter = GST_ONNXPOSTPROCESS (object);

  switch (prop_id) {
    case PROP_DRAW_RESULTS:
      g_value_set_boolean (value, filter->draw_results);
      break;
    case PROP_ORIGINAL_WIDTH:
      g_value_set_int (value, filter->original_width);
      break;
    case PROP_ORIGINAL_HEIGHT:
      g_value_set_int (value, filter->original_height);
      break;
    case PROP_CONF_THRESHOLD:
      g_value_set_float (value, filter->conf_threshold);
      break;
    case PROP_NMS_THRESHOLD:
      g_value_set_float (value, filter->nms_threshold);
      break;
    case PROP_FILTER_CLASSES:
      g_value_set_string (value, filter->filter_classes);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

const int INPUT_WIDTH = 640;
const int INPUT_HEIGHT = 640;

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
gst_onnxpostprocess_transform (GstBaseTransform * base, GstBuffer * inbuf, GstBuffer * outbuf)
{
  guint n_mem = gst_buffer_n_memory(inbuf);
  GstClockTime in_pts = GST_BUFFER_PTS(inbuf);
  
  if (n_mem < 2) {
    // Buffer doesn't contain side-loaded tensor, just copy image and pass forward
    GstMapInfo in_map, out_map;
    if (gst_buffer_map(inbuf, &in_map, GST_MAP_READ) && gst_buffer_map(outbuf, &out_map, GST_MAP_WRITE)) {
        memcpy(out_map.data, in_map.data, in_map.size);
        gst_buffer_unmap(outbuf, &out_map);
        gst_buffer_unmap(inbuf, &in_map);
    }
    return GST_FLOW_OK;
  }

  GstMemory *image_mem_in = gst_buffer_peek_memory (inbuf, 0);
  GstMemory *tensor_mem_in = gst_buffer_peek_memory (inbuf, n_mem - 1);
  GstMemory *image_mem_out = gst_buffer_peek_memory (outbuf, 0);

  GstMapInfo img_in_map;
  if (!gst_memory_map (image_mem_in, &img_in_map, GST_MAP_READ)) {
      GST_ERROR_OBJECT (base, "Failed to map input image memory");
      return GST_FLOW_ERROR;
  }

  GstMapInfo img_out_map;
  if (!gst_memory_map (image_mem_out, &img_out_map, GST_MAP_WRITE)) {
      GST_ERROR_OBJECT (base, "Failed to map output image memory");
      gst_memory_unmap (image_mem_in, &img_in_map);
      return GST_FLOW_ERROR;
  }

  // Copy image data to output
  memcpy (img_out_map.data, img_in_map.data, img_in_map.size);

  GstMapInfo tensor_map;
  if (!gst_memory_map (tensor_mem_in, &tensor_map, GST_MAP_READ)) {
      GST_ERROR_OBJECT (base, "Failed to map tensor memory");
      gst_memory_unmap (image_mem_out, &img_out_map);
      gst_memory_unmap (image_mem_in, &img_in_map);
      return GST_FLOW_ERROR;
  }

  float* output_data = (float*)tensor_map.data;
  int num_classes = 80;
  int num_boxes = 8400;

  std::vector<cv::Rect> boxes;
  std::vector<float> confidences;
  std::vector<int> class_ids;

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

      if (max_conf > GST_ONNXPOSTPROCESS (base)->conf_threshold) {
          Gstonnxpostprocess *filter = GST_ONNXPOSTPROCESS (base);
          bool allowed = true;
          if (filter->num_filter_ids > 0) {
              allowed = false;
              for (guint f = 0; f < filter->num_filter_ids; f++) {
                  if (best_class_id == filter->filter_ids[f]) {
                      allowed = true;
                      break;
                  }
              }
          }

          if (allowed) {
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
  }

  std::vector<int> indices;
  cv::dnn::NMSBoxes(boxes, confidences, GST_ONNXPOSTPROCESS (base)->conf_threshold, GST_ONNXPOSTPROCESS (base)->nms_threshold, indices);

  GST_INFO_OBJECT (base, "Detected %zu objects", indices.size());

  for (int idx : indices) {
      cv::Rect box = boxes[idx];
      int class_id = class_ids[idx];
      float conf = confidences[idx];

      /* Attach custom metadata to the output buffer using native 640x640 coordinates */
      gst_buffer_add_onnx_meta (outbuf, -1, box.x, box.y, box.width, box.height,
          conf, CLASS_NAMES[class_id].c_str(), in_pts);

      if (GST_ONNXPOSTPROCESS (base)->draw_results) {
          cv::Mat frame(INPUT_HEIGHT, INPUT_WIDTH, CV_8UC3, img_out_map.data);
          cv::rectangle(frame, box, cv::Scalar(0, 255, 0), 2);
          std::string label = CLASS_NAMES[class_id] + " " + cv::format("%.2f", conf);

          int baseLine;
          cv::Size labelSize = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseLine);
          cv::rectangle(frame, cv::Point(box.x, box.y - labelSize.height),
                        cv::Point(box.x + labelSize.width, box.y + baseLine), cv::Scalar(0, 255, 0), cv::FILLED);
          cv::putText(frame, label, cv::Point(box.x, box.y), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0), 1);
      }
  }

  gst_memory_unmap (tensor_mem_in, &tensor_map);
  gst_memory_unmap (image_mem_out, &img_out_map);
  gst_memory_unmap (image_mem_in, &img_in_map);

  return GST_FLOW_OK;
}

static gboolean
onnxpostprocess_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "onnxpostprocess", GST_RANK_NONE,
      GST_TYPE_ONNXPOSTPROCESS);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    onnxpostprocess,
    "YOLO ONNX Postprocess filter",
    onnxpostprocess_init,
    PACKAGE_VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
