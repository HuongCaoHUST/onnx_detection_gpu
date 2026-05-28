/* 
 * GStreamer
 * Copyright (C) 2026 HuongCao <<user@hostname.org>>
 */

#ifndef __GST_ONNXCLASSIFIER_H__
#define __GST_ONNXCLASSIFIER_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

/* ONNX Runtime includes */
#include <onnxruntime_cxx_api.h>
#include <map>
#include <string>

struct ClassifierTrackState {
    std::map<std::string, int> missing_item_counts;
};

G_BEGIN_DECLS

#define GST_TYPE_ONNXCLASSIFIER (gst_onnxclassifier_get_type())
G_DECLARE_FINAL_TYPE (Gstonnxclassifier, gst_onnxclassifier,
    GST, ONNXCLASSIFIER, GstBaseTransform)

struct _Gstonnxclassifier {
  GstBaseTransform element;

  /* Properties */
  gchar *model_location;
  gint input_width;
  gint input_height;
  gchar *labels;
  gchar **label_list;
  guint num_labels;
  gfloat threshold;

  /* ONNX Runtime state */
  Ort::Env *env;
  Ort::Session *session;
  Ort::MemoryInfo *memory_info;
  Ort::AllocatorWithDefaultOptions *allocator;
  
  std::map<int, ClassifierTrackState> *track_states;
};

G_END_DECLS

#endif /* __GST_ONNXCLASSIFIER_H__ */
