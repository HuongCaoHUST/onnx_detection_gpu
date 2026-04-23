/* 
 * GStreamer
 * Copyright (C) 2026 HuongCao <<user@hostname.org>>
 */

#ifndef __GST_ONNX_META_H__
#define __GST_ONNX_META_H__

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _GstOnnxMeta GstOnnxMeta;

struct _GstOnnxMeta {
  GstMeta meta;

  gint track_id;
  gint x, y, w, h;
  gchar *label;
  GstClockTime pts;
};

GType gst_onnx_meta_api_get_type (void);
#define GST_ONNX_META_API_TYPE (gst_onnx_meta_api_get_type())

const GstMetaInfo * gst_onnx_meta_get_info (void);
#define GST_ONNX_META_INFO (gst_onnx_meta_get_info())

#define gst_buffer_get_onnx_meta(b) \
  ((GstOnnxMeta*)gst_buffer_get_meta((b),GST_ONNX_META_API_TYPE))

GstOnnxMeta * gst_buffer_add_onnx_meta (GstBuffer * buffer, gint track_id, gint x, gint y, gint w, gint h, const gchar * label, GstClockTime pts);

G_END_DECLS

#endif /* __GST_ONNX_META_H__ */
