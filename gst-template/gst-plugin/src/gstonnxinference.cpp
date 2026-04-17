#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/base.h>
#include <gst/video/video.h>
#include <gst/controller/controller.h>

#include "gstonnxinference.h"
#include <opencv2/opencv.hpp>
#include <vector>
#include <iostream>

#define INPUT_WIDTH 640
#define INPUT_HEIGHT 640

GST_DEBUG_CATEGORY_STATIC (gst_onnxinference_debug);
#define GST_CAT_DEFAULT gst_onnxinference_debug

enum
{
  PROP_0,
  PROP_MODEL_LOCATION,
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

#define gst_onnxinference_parent_class parent_class
G_DEFINE_TYPE (Gstonnxinference, gst_onnxinference, GST_TYPE_BASE_TRANSFORM);
GST_ELEMENT_REGISTER_DEFINE (onnxinference, "onnxinference", GST_RANK_NONE,
    GST_TYPE_ONNXINFERENCE);

static void gst_onnxinference_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_onnxinference_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_onnxinference_finalize (GObject * object);

static gboolean gst_onnxinference_start (GstBaseTransform * base);
static gboolean gst_onnxinference_stop (GstBaseTransform * base);
static GstFlowReturn gst_onnxinference_transform_ip (GstBaseTransform * base, GstBuffer * outbuf);

static void
gst_onnxinference_class_init (GstonnxinferenceClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBaseTransformClass *basetransform_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = gst_onnxinference_set_property;
  gobject_class->get_property = gst_onnxinference_get_property;
  gobject_class->finalize = gst_onnxinference_finalize;

  g_object_class_install_property (gobject_class, PROP_MODEL_LOCATION,
      g_param_spec_string ("model-location", "Model Location", 
          "Path to the YOLO ONNX model", NULL,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

  gst_element_class_set_details_simple (gstelement_class,
      "ONNX Inference", "Filter/Video",
      "Performs YOLO ONNX Inference on 640x640 RGB image", "HuongCao <<user@hostname.org>>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));

  basetransform_class->start = GST_DEBUG_FUNCPTR (gst_onnxinference_start);
  basetransform_class->stop = GST_DEBUG_FUNCPTR (gst_onnxinference_stop);
  basetransform_class->transform_ip = GST_DEBUG_FUNCPTR (gst_onnxinference_transform_ip);

  GST_DEBUG_CATEGORY_INIT (gst_onnxinference_debug, "onnxinference", 0, "onnxinference element");
}

static void
gst_onnxinference_init (Gstonnxinference * filter)
{
  filter->model_location = NULL;
  filter->env = NULL;
  filter->session = NULL;
  filter->memory_info = NULL;
  filter->allocator = NULL;
}

static void
gst_onnxinference_finalize (GObject * object)
{
  Gstonnxinference *filter = GST_ONNXINFERENCE (object);

  g_free (filter->model_location);
  
  if (filter->session) delete filter->session;
  if (filter->env) delete filter->env;
  if (filter->memory_info) delete filter->memory_info;
  if (filter->allocator) delete filter->allocator;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_onnxinference_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  Gstonnxinference *filter = GST_ONNXINFERENCE (object);

  switch (prop_id) {
    case PROP_MODEL_LOCATION:
      g_free (filter->model_location);
      filter->model_location = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_onnxinference_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  Gstonnxinference *filter = GST_ONNXINFERENCE (object);

  switch (prop_id) {
    case PROP_MODEL_LOCATION:
      g_value_set_string (value, filter->model_location);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_onnxinference_start (GstBaseTransform * base)
{
  Gstonnxinference *filter = GST_ONNXINFERENCE (base);

  if (!filter->model_location) {
    GST_ERROR_OBJECT (filter, "Model location is not set.");
    return FALSE;
  }

  try {
    filter->env = new Ort::Env(ORT_LOGGING_LEVEL_WARNING, "YOLO11");
    Ort::SessionOptions session_options;
    
    // Enable CUDA provider
    OrtCUDAProviderOptions cuda_options;
    cuda_options.device_id = 0;
    session_options.AppendExecutionProvider_CUDA(cuda_options);
    
    filter->session = new Ort::Session(*filter->env, filter->model_location, session_options);
    filter->allocator = new Ort::AllocatorWithDefaultOptions();
    filter->memory_info = new Ort::MemoryInfo(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault));
    GST_INFO_OBJECT (filter, "ONNX session initialized successfully.");
  } catch (const Ort::Exception& e) {
    GST_ERROR_OBJECT (filter, "ONNX Runtime initialization failed: %s", e.what());
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_onnxinference_stop (GstBaseTransform * base)
{
  Gstonnxinference *filter = GST_ONNXINFERENCE (base);
  
  if (filter->session) { delete filter->session; filter->session = NULL; }
  if (filter->env) { delete filter->env; filter->env = NULL; }
  if (filter->memory_info) { delete filter->memory_info; filter->memory_info = NULL; }
  if (filter->allocator) { delete filter->allocator; filter->allocator = NULL; }
  
  return TRUE;
}

static GstFlowReturn
gst_onnxinference_transform_ip (GstBaseTransform * base, GstBuffer * outbuf)
{
  Gstonnxinference *filter = GST_ONNXINFERENCE (base);

  if (!filter->session) {
    return GST_FLOW_ERROR;
  }

  GstMapInfo map;
  if (!gst_buffer_map (outbuf, &map, GST_MAP_READ)) {
      GST_ERROR_OBJECT (filter, "Failed to map buffer for reading");
      return GST_FLOW_ERROR;
  }

  try {
      cv::Mat frame(INPUT_HEIGHT, INPUT_WIDTH, CV_8UC3, map.data);

      cv::Mat blob;
      frame.convertTo(blob, CV_32F, 1.0f / 255.0f);

      // Convert HWC to CHW
      std::vector<cv::Mat> chw;
      cv::split(blob, chw);
      
      std::vector<float> input_tensor_values;
      size_t image_size = INPUT_HEIGHT * INPUT_WIDTH;
      input_tensor_values.reserve(3 * image_size);
      for (int i = 0; i < 3; ++i) {
          input_tensor_values.insert(input_tensor_values.end(), (float*)chw[i].datastart, (float*)chw[i].dataend);
      }

      std::vector<int64_t> input_shape = {1, 3, INPUT_HEIGHT, INPUT_WIDTH};

      Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
          *filter->memory_info, input_tensor_values.data(), input_tensor_values.size(), input_shape.data(), input_shape.size());

      std::string input_name_str = filter->session->GetInputNameAllocated(0, *filter->allocator).get();
      std::string output_name_str = filter->session->GetOutputNameAllocated(0, *filter->allocator).get();
      
      const char* input_names[] = { input_name_str.c_str() };
      const char* output_names[] = { output_name_str.c_str() };

      auto output_tensors = filter->session->Run(Ort::RunOptions{nullptr}, input_names, &input_tensor, 1, output_names, 1);
      
      float* output_data = output_tensors[0].GetTensorMutableData<float>();
      
      // Output shape is [1, 84, 8400]
      size_t tensor_size_bytes = 84 * 8400 * sizeof(float);
      
      float* tensor_copy = (float*) g_malloc(tensor_size_bytes);
      memcpy(tensor_copy, output_data, tensor_size_bytes);
      
      gst_buffer_unmap (outbuf, &map);
      
      // Make writable if necessary
      outbuf = gst_buffer_make_writable(outbuf);
      
      GstMemory *out_mem = gst_memory_new_wrapped ((GstMemoryFlags)0, tensor_copy, tensor_size_bytes, 0, tensor_size_bytes, tensor_copy, g_free);
      gst_buffer_append_memory (outbuf, out_mem);
      
  } catch (const std::exception& e) {
      GST_ERROR_OBJECT (filter, "Inference exception: %s", e.what());
      gst_buffer_unmap (outbuf, &map);
      return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;
}

static gboolean
onnxinference_init (GstPlugin * onnxinference)
{
  return GST_ELEMENT_REGISTER (onnxinference, onnxinference);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    onnxinference,
    "YOLO ONNX Inference wrapper",
    onnxinference_init,
    PACKAGE_VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
