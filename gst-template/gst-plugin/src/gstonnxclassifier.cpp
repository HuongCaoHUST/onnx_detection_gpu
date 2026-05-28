#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/base.h>
#include <gst/video/video.h>

#include "gstonnxclassifier.h"
#include "gstonnxmeta.h"

#include <opencv2/opencv.hpp>
#include <vector>
#include <iostream>
#include <string>
#include <cmath>

GST_DEBUG_CATEGORY_STATIC (gst_onnxclassifier_debug);
#define GST_CAT_DEFAULT gst_onnxclassifier_debug

enum
{
  PROP_0,
  PROP_MODEL_LOCATION,
  PROP_INPUT_WIDTH,
  PROP_INPUT_HEIGHT,
  PROP_LABELS,
  PROP_THRESHOLD,
};

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

#define gst_onnxclassifier_parent_class parent_class
G_DEFINE_TYPE (Gstonnxclassifier, gst_onnxclassifier, GST_TYPE_BASE_TRANSFORM);

static void gst_onnxclassifier_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_onnxclassifier_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_onnxclassifier_finalize (GObject * object);

static gboolean gst_onnxclassifier_start (GstBaseTransform * base);
static gboolean gst_onnxclassifier_stop (GstBaseTransform * base);
static gboolean gst_onnxclassifier_transform_size (GstBaseTransform * base, GstPadDirection direction, GstCaps * caps, gsize size, GstCaps * othercaps, gsize * othersize);
static GstFlowReturn gst_onnxclassifier_transform_ip (GstBaseTransform * base, GstBuffer * buf);

static void
gst_onnxclassifier_class_init (GstonnxclassifierClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBaseTransformClass *basetransform_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = gst_onnxclassifier_set_property;
  gobject_class->get_property = gst_onnxclassifier_get_property;
  gobject_class->finalize = gst_onnxclassifier_finalize;

  g_object_class_install_property (gobject_class, PROP_MODEL_LOCATION,
      g_param_spec_string ("model-location", "Model Location", 
          "Path to the Classification ONNX model", NULL,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_INPUT_WIDTH,
      g_param_spec_int ("input-width", "Input Width",
          "Input width of the classifier model", 1, 4096, 224,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_INPUT_HEIGHT,
      g_param_spec_int ("input-height", "Input Height",
          "Input height of the classifier model", 1, 4096, 224,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_LABELS,
      g_param_spec_string ("labels", "Labels", 
          "Comma-separated list of classification labels (e.g., 'No Hardhat,Hardhat')", NULL,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_THRESHOLD,
      g_param_spec_float ("threshold", "Threshold",
          "Classification confidence threshold", 0.0f, 1.0f, 0.5f,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_details_simple (gstelement_class,
      "ONNX Classifier", "Filter/Video",
      "Performs ONNX Classification Inference on cropped ROIs", "HuongCao <<user@hostname.org>>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));

  basetransform_class->start = GST_DEBUG_FUNCPTR (gst_onnxclassifier_start);
  basetransform_class->stop = GST_DEBUG_FUNCPTR (gst_onnxclassifier_stop);
  basetransform_class->transform_size = GST_DEBUG_FUNCPTR (gst_onnxclassifier_transform_size);
  basetransform_class->transform_ip = GST_DEBUG_FUNCPTR (gst_onnxclassifier_transform_ip);

  GST_DEBUG_CATEGORY_INIT (gst_onnxclassifier_debug, "onnxclassifier", 0, "onnxclassifier element");
}

static void
gst_onnxclassifier_init (Gstonnxclassifier * filter)
{
  filter->model_location = NULL;
  filter->input_width = 224;
  filter->input_height = 224;
  filter->labels = NULL;
  filter->label_list = NULL;
  filter->num_labels = 0;
  filter->threshold = 0.5f;

  filter->env = NULL;
  filter->session = NULL;
  filter->memory_info = NULL;
  filter->allocator = NULL;

  filter->track_states = new std::map<int, ClassifierTrackState>();
}

static void
gst_onnxclassifier_finalize (GObject * object)
{
  Gstonnxclassifier *filter = GST_ONNXCLASSIFIER (object);

  g_free (filter->model_location);
  g_free (filter->labels);
  g_strfreev (filter->label_list);
  
  if (filter->session) delete filter->session;
  if (filter->env) delete filter->env;
  if (filter->memory_info) delete filter->memory_info;
  if (filter->allocator) delete filter->allocator;
  
  if (filter->track_states) {
      delete filter->track_states;
      filter->track_states = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_onnxclassifier_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  Gstonnxclassifier *filter = GST_ONNXCLASSIFIER (object);

  switch (prop_id) {
    case PROP_MODEL_LOCATION:
      g_free (filter->model_location);
      filter->model_location = g_value_dup_string (value);
      break;
    case PROP_INPUT_WIDTH:
      filter->input_width = g_value_get_int (value);
      break;
    case PROP_INPUT_HEIGHT:
      filter->input_height = g_value_get_int (value);
      break;
    case PROP_LABELS:
      g_free (filter->labels);
      g_strfreev (filter->label_list);
      filter->labels = g_value_dup_string (value);
      if (filter->labels) {
          filter->label_list = g_strsplit(filter->labels, ",", -1);
          filter->num_labels = g_strv_length(filter->label_list);
      } else {
          filter->label_list = NULL;
          filter->num_labels = 0;
      }
      break;
    case PROP_THRESHOLD:
      filter->threshold = g_value_get_float (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_onnxclassifier_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  Gstonnxclassifier *filter = GST_ONNXCLASSIFIER (object);

  switch (prop_id) {
    case PROP_MODEL_LOCATION:
      g_value_set_string (value, filter->model_location);
      break;
    case PROP_INPUT_WIDTH:
      g_value_set_int (value, filter->input_width);
      break;
    case PROP_INPUT_HEIGHT:
      g_value_set_int (value, filter->input_height);
      break;
    case PROP_LABELS:
      g_value_set_string (value, filter->labels);
      break;
    case PROP_THRESHOLD:
      g_value_set_float (value, filter->threshold);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_onnxclassifier_start (GstBaseTransform * base)
{
  Gstonnxclassifier *filter = GST_ONNXCLASSIFIER (base);

  if (!filter->model_location) {
    GST_ERROR_OBJECT (filter, "Model location is not set.");
    return FALSE;
  }

  try {
    filter->env = new Ort::Env(ORT_LOGGING_LEVEL_WARNING, "Classifier");
    Ort::SessionOptions session_options;
    
    // Enable CUDA provider if available, otherwise fallback to CPU
    const char* disable_cuda = getenv("ORT_DISABLE_CUDA");
    if (!disable_cuda || strcmp(disable_cuda, "1") != 0) {
        try {
          OrtCUDAProviderOptions cuda_options;
          cuda_options.device_id = 0;
          session_options.AppendExecutionProvider_CUDA(cuda_options);
          GST_INFO_OBJECT (filter, "Using CUDA execution provider for classification");
        } catch (const Ort::Exception& e) {
          GST_WARNING_OBJECT (filter, "CUDA provider not available (%s). Using CPU instead.", e.what());
        }
    } else {
        GST_INFO_OBJECT (filter, "CUDA execution provider disabled via ORT_DISABLE_CUDA");
    }
    
    filter->session = new Ort::Session(*filter->env, filter->model_location, session_options);
    filter->allocator = new Ort::AllocatorWithDefaultOptions();
    filter->memory_info = new Ort::MemoryInfo(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault));
    GST_INFO_OBJECT (filter, "ONNX classifier session initialized successfully.");
  } catch (const Ort::Exception& e) {
    GST_ERROR_OBJECT (filter, "ONNX Runtime initialization failed: %s", e.what());
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_onnxclassifier_stop (GstBaseTransform * base)
{
  Gstonnxclassifier *filter = GST_ONNXCLASSIFIER (base);

  if (filter->session) { delete filter->session; filter->session = NULL; }
  if (filter->env) { delete filter->env; filter->env = NULL; }
  if (filter->memory_info) { delete filter->memory_info; filter->memory_info = NULL; }
  if (filter->allocator) { delete filter->allocator; filter->allocator = NULL; }

  return TRUE;
}

static gboolean
gst_onnxclassifier_transform_size (GstBaseTransform * base, GstPadDirection direction, GstCaps * caps, gsize size, GstCaps * othercaps, gsize * othersize)
{
  *othersize = size;
  return TRUE;
}

static GstFlowReturn
gst_onnxclassifier_transform_ip (GstBaseTransform * base, GstBuffer * buf)
{
  Gstonnxclassifier *filter = GST_ONNXCLASSIFIER (base);

  if (!filter->session) {
    return GST_FLOW_ERROR;
  }

  // Iterate over all ONNX Metadata
  gpointer state = NULL;
  GstMeta *meta;
  std::vector<GstOnnxMeta*> metas;
  
  while ((meta = gst_buffer_iterate_meta (buf, &state))) {
    if (meta->info->api == GST_ONNX_META_API_TYPE) {
      GstOnnxMeta *ometa = (GstOnnxMeta *) meta;
      metas.push_back(ometa);
    }
  }

  if (metas.empty()) {
      return GST_FLOW_OK;
  }

  GstMapInfo map;
  if (!gst_buffer_map (buf, &map, GST_MAP_READ)) {
      GST_ERROR_OBJECT (filter, "Failed to map input buffer for reading");
      return GST_FLOW_ERROR;
  }

  try {
      // Assuming 640x640 RGB image
      cv::Mat frame(640, 640, CV_8UC3, map.data);

      for (auto *ometa : metas) {
          int x = MAX(0, ometa->x);
          int y = MAX(0, ometa->y);
          int w = MIN(640 - x, ometa->w);
          int h = MIN(640 - y, ometa->h);

          if (w <= 0 || h <= 0) continue;

          cv::Rect bbox(x, y, w, h);
          cv::Mat roi = frame(bbox);
          
          cv::Mat resized_roi;
          cv::resize(roi, resized_roi, cv::Size(filter->input_width, filter->input_height));
          
          cv::Mat blob;
          // Scale to [0, 1]
          resized_roi.convertTo(blob, CV_32F, 1.0f / 255.0f);
          // Apply ImageNet normalization (mean and std)
          cv::Scalar mean(0.485, 0.456, 0.406);
          cv::Scalar std_dev(0.229, 0.224, 0.225);
          cv::subtract(blob, mean, blob);
          cv::divide(blob, std_dev, blob);

          // Convert HWC to CHW
          std::vector<cv::Mat> chw;
          cv::split(blob, chw);
          
          std::vector<float> input_tensor_values;
          size_t image_size = filter->input_width * filter->input_height;
          input_tensor_values.reserve(3 * image_size);
          for (int i = 0; i < 3; ++i) {
              input_tensor_values.insert(input_tensor_values.end(), (float*)chw[i].datastart, (float*)chw[i].dataend);
          }

          std::vector<int64_t> input_shape = {1, 3, filter->input_height, filter->input_width};

          Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
              *filter->memory_info, input_tensor_values.data(), input_tensor_values.size(), input_shape.data(), input_shape.size());

          std::string input_name_str = filter->session->GetInputNameAllocated(0, *filter->allocator).get();
          std::string output_name_str = filter->session->GetOutputNameAllocated(0, *filter->allocator).get();
          
          const char* input_names[] = { input_name_str.c_str() };
          const char* output_names[] = { output_name_str.c_str() };

          auto output_tensors = filter->session->Run(Ort::RunOptions{nullptr}, input_names, &input_tensor, 1, output_names, 1);
          
          float* output_data = output_tensors[0].GetTensorMutableData<float>();
          
          // Assuming output shape is [1, num_classes]
          auto type_info = output_tensors[0].GetTensorTypeAndShapeInfo();
          auto shape = type_info.GetShape();
          int num_classes = shape.size() > 1 ? shape[1] : shape[0]; // handle both [1, N] and [N]
          
          std::vector<std::string> missing_items;
          float threshold = filter->threshold; // Sử dụng giá trị từ thuộc tính GStreamer

          for (int c = 0; c < num_classes; ++c) {
              // PyTorch BCEWithLogitsLoss implies raw logit output, need sigmoid
              float prob = 1.0f / (1.0f + std::exp(-output_data[c]));
              if (prob < threshold) {
                  std::string label_name = (filter->label_list && c < (int)filter->num_labels) ? 
                                           filter->label_list[c] : "Class_" + std::to_string(c);
                  
                  // Tạm thời bỏ qua nhãn "Mask" theo yêu cầu
                  if (label_name != "Mask" && label_name != "mask") {
                      missing_items.push_back(label_name);
                  }
              }
          }

          std::string class_str;
          bool current_is_safe = missing_items.empty();
          bool report_safe = current_is_safe;
          
          if (ometa->track_id >= 0) {
              if (filter->track_states->find(ometa->track_id) == filter->track_states->end()) {
                  (*filter->track_states)[ometa->track_id] = {0};
              }
              auto& state = (*filter->track_states)[ometa->track_id];
              
              if (current_is_safe) {
                  state.consecutive_unsafe = 0;
                  report_safe = true;
              } else {
                  state.consecutive_unsafe++;
                  if (state.consecutive_unsafe >= 5) {
                      report_safe = false;
                  } else {
                      // Still report as safe to avoid false alarms
                      report_safe = true;
                  }
              }
          }

          if (report_safe) {
              class_str = "SAFE";
          } else {
              class_str = "UNSAFE (-";
              for (size_t i = 0; i < missing_items.size(); ++i) {
                  class_str += missing_items[i];
                  if (i < missing_items.size() - 1) class_str += ",";
              }
              class_str += ")";
          }

          std::string new_label = std::string(ometa->label ? ometa->label : "") + " [" + class_str + "]";
          
          // GstOnnxMeta allocates label with g_strdup
          g_free(ometa->label);
          ometa->label = g_strdup(new_label.c_str());
      }
      
  } catch (const std::exception& e) {
      GST_ERROR_OBJECT (filter, "Classification Inference exception: %s", e.what());
  }

  gst_buffer_unmap (buf, &map);

  return GST_FLOW_OK;
}

static gboolean
onnxclassifier_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "onnxclassifier", GST_RANK_NONE,
      GST_TYPE_ONNXCLASSIFIER);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    onnxclassifier,
    "ONNX Image Classifier wrapper",
    onnxclassifier_init,
    PACKAGE_VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
