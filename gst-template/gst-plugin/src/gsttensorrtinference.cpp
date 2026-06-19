#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gsttensorrtinference.h"

#include <NvInfer.h>
#include <NvOnnxParser.h>
#include <cuda_runtime_api.h>
#include <gst/gst.h>
#include <opencv2/opencv.hpp>

#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {
constexpr int kInputWidth = 640;
constexpr int kInputHeight = 640;

class Logger final : public nvinfer1::ILogger {
 public:
  void log(Severity severity, const char* message) noexcept override {
    if (severity <= Severity::kWARNING)
      g_printerr("TensorRT: %s\n", message);
  }
};

template <typename T> struct TrtDestroy {
  void operator()(T* value) const { if (value) value->destroy(); }
};
template <typename T> using TrtPtr = std::unique_ptr<T, TrtDestroy<T>>;

struct TrtState {
  Logger logger;
  TrtPtr<nvinfer1::IRuntime> runtime;
  TrtPtr<nvinfer1::ICudaEngine> engine;
  TrtPtr<nvinfer1::IExecutionContext> context;
  cudaStream_t stream{};
  void* bindings[2]{};
  int input_index{-1};
  int output_index{-1};
  size_t input_elements{};
  size_t output_elements{};

  ~TrtState() {
    for (void*& binding : bindings) {
      if (binding) cudaFree(binding);
      binding = nullptr;
    }
    if (stream) cudaStreamDestroy(stream);
  }
};

bool cuda_ok(cudaError_t result, GstElement* element, const char* operation) {
  if (result == cudaSuccess) return true;
  GST_ELEMENT_ERROR(element, RESOURCE, FAILED,
      ("CUDA operation failed: %s", operation), ("%s", cudaGetErrorString(result)));
  return false;
}

size_t volume(const nvinfer1::Dims& dims) {
  size_t result = 1;
  for (int i = 0; i < dims.nbDims; ++i) {
    if (dims.d[i] <= 0) return 0;
    result *= static_cast<size_t>(dims.d[i]);
  }
  return result;
}

std::vector<char> read_file(const gchar* path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) return {};
  const auto size = input.tellg();
  if (size <= 0) return {};
  std::vector<char> data(static_cast<size_t>(size));
  input.seekg(0);
  input.read(data.data(), size);
  return data;
}

bool write_file(const gchar* path, const void* data, size_t size) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  return output && output.write(static_cast<const char*>(data), size).good();
}

TrtPtr<nvinfer1::ICudaEngine> build_engine(TrtState& state, const gchar* model,
                                           const gchar* cache, GstElement* element) {
  auto builder = TrtPtr<nvinfer1::IBuilder>(nvinfer1::createInferBuilder(state.logger));
  if (!builder) return {};
  const auto flags = 1U << static_cast<unsigned>(
      nvinfer1::NetworkDefinitionCreationFlag::kEXPLICIT_BATCH);
  auto network = TrtPtr<nvinfer1::INetworkDefinition>(builder->createNetworkV2(flags));
  auto parser = TrtPtr<nvonnxparser::IParser>(nvonnxparser::createParser(*network, state.logger));
  auto config = TrtPtr<nvinfer1::IBuilderConfig>(builder->createBuilderConfig());
  if (!network || !parser || !config || !parser->parseFromFile(model,
      static_cast<int>(nvinfer1::ILogger::Severity::kWARNING))) {
    GST_ELEMENT_ERROR(element, RESOURCE, FAILED, ("Cannot parse ONNX model %s", model), (nullptr));
    return {};
  }
  config->setMaxWorkspaceSize(512ULL * 1024 * 1024);
  if (builder->platformHasFastFp16()) config->setFlag(nvinfer1::BuilderFlag::kFP16);
  auto engine = TrtPtr<nvinfer1::ICudaEngine>(builder->buildEngineWithConfig(*network, *config));
  if (engine && cache && *cache) {
    auto serialized = TrtPtr<nvinfer1::IHostMemory>(engine->serialize());
    if (!serialized || !write_file(cache, serialized->data(), serialized->size()))
      GST_WARNING_OBJECT(element, "Could not write TensorRT engine cache: %s", cache);
  }
  return engine;
}
}  // namespace

GST_DEBUG_CATEGORY_STATIC(gst_onnxinference_debug);
#define GST_CAT_DEFAULT gst_onnxinference_debug

enum { PROP_0, PROP_MODEL_LOCATION, PROP_ENGINE_CACHE };

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK,
    GST_PAD_ALWAYS, GST_STATIC_CAPS("video/x-raw,format=(string)RGB,width=(int)640,height=(int)640"));
static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC,
    GST_PAD_ALWAYS, GST_STATIC_CAPS("video/x-raw,format=(string)RGB,width=(int)640,height=(int)640"));

#define gst_onnxinference_parent_class parent_class
G_DEFINE_TYPE(Gstonnxinference, gst_onnxinference, GST_TYPE_BASE_TRANSFORM)

static void set_property(GObject* object, guint id, const GValue* value, GParamSpec* pspec) {
  auto* self = GST_ONNXINFERENCE(object);
  gchar** target = id == PROP_MODEL_LOCATION ? &self->model_location : &self->engine_cache;
  if (id == PROP_MODEL_LOCATION || id == PROP_ENGINE_CACHE) {
    g_free(*target); *target = g_value_dup_string(value);
  } else G_OBJECT_WARN_INVALID_PROPERTY_ID(object, id, pspec);
}

static void get_property(GObject* object, guint id, GValue* value, GParamSpec* pspec) {
  auto* self = GST_ONNXINFERENCE(object);
  if (id == PROP_MODEL_LOCATION) g_value_set_string(value, self->model_location);
  else if (id == PROP_ENGINE_CACHE) g_value_set_string(value, self->engine_cache);
  else G_OBJECT_WARN_INVALID_PROPERTY_ID(object, id, pspec);
}

static gboolean start(GstBaseTransform* base) {
  auto* self = GST_ONNXINFERENCE(base);
  if (!self->model_location) return FALSE;
  auto state = std::make_unique<TrtState>();
  state->runtime.reset(nvinfer1::createInferRuntime(state->logger));
  if (!state->runtime) return FALSE;
  if (self->engine_cache && g_file_test(self->engine_cache, G_FILE_TEST_EXISTS)) {
    auto data = read_file(self->engine_cache);
    if (!data.empty()) state->engine.reset(state->runtime->deserializeCudaEngine(data.data(), data.size()));
  }
  if (!state->engine) state->engine = build_engine(*state, self->model_location,
                                                    self->engine_cache, GST_ELEMENT(self));
  if (!state->engine || state->engine->getNbBindings() != 2) {
    GST_ELEMENT_ERROR(self, RESOURCE, FAILED, ("TensorRT requires a model with exactly one input and one output"), (nullptr));
    return FALSE;
  }
  state->input_index = state->engine->bindingIsInput(0) ? 0 : 1;
  state->output_index = 1 - state->input_index;
  if (state->engine->getBindingDataType(state->input_index) != nvinfer1::DataType::kFLOAT ||
      state->engine->getBindingDataType(state->output_index) != nvinfer1::DataType::kFLOAT) {
    GST_ELEMENT_ERROR(self, RESOURCE, FAILED,
        ("TensorRT model input/output bindings must be float32"), (nullptr));
    return FALSE;
  }
  state->input_elements = volume(state->engine->getBindingDimensions(state->input_index));
  state->output_elements = volume(state->engine->getBindingDimensions(state->output_index));
  if (state->input_elements != 3ULL * kInputWidth * kInputHeight || state->output_elements == 0) {
    GST_ELEMENT_ERROR(self, RESOURCE, FAILED, ("Unexpected TensorRT model dimensions"), (nullptr));
    return FALSE;
  }
  state->context.reset(state->engine->createExecutionContext());
  if (!state->context || !cuda_ok(cudaStreamCreate(&state->stream), GST_ELEMENT(self), "create stream") ||
      !cuda_ok(cudaMalloc(&state->bindings[state->input_index], state->input_elements * sizeof(float)), GST_ELEMENT(self), "allocate input") ||
      !cuda_ok(cudaMalloc(&state->bindings[state->output_index], state->output_elements * sizeof(float)), GST_ELEMENT(self), "allocate output")) return FALSE;
  GST_INFO_OBJECT(self, "TensorRT GPU active; input=%zu output=%zu FP16=%s",
      state->input_elements, state->output_elements, state->engine->hasImplicitBatchDimension() ? "implicit" : "enabled when supported");
  self->trt_state = state.release();
  return TRUE;
}

static gboolean stop(GstBaseTransform* base) {
  auto* self = GST_ONNXINFERENCE(base);
  delete static_cast<TrtState*>(self->trt_state); self->trt_state = nullptr;
  return TRUE;
}

static gboolean transform_size(GstBaseTransform*, GstPadDirection, GstCaps*, gsize size,
                               GstCaps*, gsize* other_size) {
  *other_size = size;
  return TRUE;
}

static GstFlowReturn transform(GstBaseTransform* base, GstBuffer* inbuf, GstBuffer* outbuf) {
  auto* self = GST_ONNXINFERENCE(base);
  auto* state = static_cast<TrtState*>(self->trt_state);
  GstMapInfo input_map{}, output_map{};
  if (!state || !gst_buffer_map(inbuf, &input_map, GST_MAP_READ) ||
      !gst_buffer_map(outbuf, &output_map, GST_MAP_WRITE)) return GST_FLOW_ERROR;
  memcpy(output_map.data, input_map.data, input_map.size);
  cv::Mat rgb(kInputHeight, kInputWidth, CV_8UC3, input_map.data);
  cv::Mat fp32; rgb.convertTo(fp32, CV_32F, 1.0 / 255.0);
  std::vector<cv::Mat> channels; cv::split(fp32, channels);
  std::vector<float> chw; chw.reserve(state->input_elements);
  for (const auto& channel : channels) {
    const float* channel_data = channel.ptr<float>(0);
    chw.insert(chw.end(), channel_data, channel_data + channel.total());
  }
  std::vector<float> result(state->output_elements);
  bool ok = cuda_ok(cudaMemcpyAsync(state->bindings[state->input_index], chw.data(),
      chw.size() * sizeof(float), cudaMemcpyHostToDevice, state->stream), GST_ELEMENT(self), "upload input") &&
      state->context->enqueueV2(state->bindings, state->stream, nullptr) &&
      cuda_ok(cudaMemcpyAsync(result.data(), state->bindings[state->output_index],
      result.size() * sizeof(float), cudaMemcpyDeviceToHost, state->stream), GST_ELEMENT(self), "download output") &&
      cuda_ok(cudaStreamSynchronize(state->stream), GST_ELEMENT(self), "synchronize");
  gst_buffer_unmap(outbuf, &output_map); gst_buffer_unmap(inbuf, &input_map);
  if (!ok) return GST_FLOW_ERROR;
  const gsize bytes = result.size() * sizeof(float);
  auto* copy = static_cast<float*>(g_malloc(bytes)); memcpy(copy, result.data(), bytes);
  gst_buffer_append_memory(outbuf, gst_memory_new_wrapped((GstMemoryFlags)0, copy, bytes, 0, bytes, copy, g_free));
  return GST_FLOW_OK;
}

static void finalize(GObject* object) {
  auto* self = GST_ONNXINFERENCE(object);
  delete static_cast<TrtState*>(self->trt_state);
  g_free(self->model_location); g_free(self->engine_cache);
  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void gst_onnxinference_class_init(GstonnxinferenceClass* klass) {
  auto* object = G_OBJECT_CLASS(klass); auto* element = GST_ELEMENT_CLASS(klass);
  auto* transform_class = GST_BASE_TRANSFORM_CLASS(klass);
  object->set_property = set_property; object->get_property = get_property; object->finalize = finalize;
  g_object_class_install_property(object, PROP_MODEL_LOCATION, g_param_spec_string("model-location", "Model", "ONNX model path", nullptr, (GParamFlags)(G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY)));
  g_object_class_install_property(object, PROP_ENGINE_CACHE, g_param_spec_string("engine-cache", "Engine cache", "TensorRT engine cache path", nullptr, (GParamFlags)(G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY)));
  gst_element_class_set_details_simple(element, "TensorRT ONNX Inference", "Filter/Video", "YOLO inference on Jetson GPU", "HuongCao");
  gst_element_class_add_pad_template(element, gst_static_pad_template_get(&sink_template));
  gst_element_class_add_pad_template(element, gst_static_pad_template_get(&src_template));
  transform_class->start = start; transform_class->stop = stop;
  transform_class->transform_size = transform_size; transform_class->transform = transform;
  GST_DEBUG_CATEGORY_INIT(gst_onnxinference_debug, "onnxinference", 0, "TensorRT inference");
}

static void gst_onnxinference_init(Gstonnxinference* self) {
  self->model_location = nullptr; self->engine_cache = nullptr; self->trt_state = nullptr;
}

static gboolean plugin_init(GstPlugin* plugin) {
  return gst_element_register(plugin, "onnxinference", GST_RANK_NONE, GST_TYPE_ONNXINFERENCE);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, onnxinference,
    "TensorRT YOLO inference", plugin_init, PACKAGE_VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
