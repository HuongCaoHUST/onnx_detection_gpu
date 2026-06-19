#ifndef __GST_TENSORRT_INFERENCE_H__
#define __GST_TENSORRT_INFERENCE_H__

#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

#define GST_TYPE_ONNXINFERENCE (gst_onnxinference_get_type())
G_DECLARE_FINAL_TYPE (Gstonnxinference, gst_onnxinference,
    GST, ONNXINFERENCE, GstBaseTransform)

struct _Gstonnxinference {
  GstBaseTransform parent;
  gchar *model_location;
  gchar *engine_cache;
  gpointer trt_state;
};

G_END_DECLS

#endif
