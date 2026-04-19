/*
 * GStreamer
 * Copyright (C) 2026 HuongCao <<user@hostname.org>>
 */

#include <gst/gst.h>
#include "gstonnxmeta.h"

static gboolean
gst_onnx_meta_init (GstMeta * meta, gpointer params, GstBuffer * buffer)
{
  GstOnnxMeta *emeta = (GstOnnxMeta *) meta;
  emeta->x = emeta->y = emeta->w = emeta->h = 0;
  emeta->label = NULL;
  return TRUE;
}

static void
gst_onnx_meta_free (GstMeta * meta, GstBuffer * buffer)
{
  GstOnnxMeta *emeta = (GstOnnxMeta *) meta;
  g_free (emeta->label);
}

static gboolean
gst_onnx_meta_transform (GstBuffer * transbuf, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GstOnnxMeta *emeta = (GstOnnxMeta *) meta;
  gst_buffer_add_onnx_meta (transbuf, emeta->x, emeta->y, emeta->w, emeta->h, emeta->label);
  return TRUE;
}

GType
gst_onnx_meta_api_get_type (void)
{
  static GType type = 0;
  static const gchar *tags[] = { NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstOnnxMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

const GstMetaInfo *
gst_onnx_meta_get_info (void)
{
  static const GstMetaInfo *onnx_meta_info = NULL;

  if (g_once_init_enter (&onnx_meta_info)) {
    const GstMetaInfo *meta_info = gst_meta_register (GST_ONNX_META_API_TYPE,
        "GstOnnxMeta", sizeof (GstOnnxMeta),
        gst_onnx_meta_init,
        gst_onnx_meta_free,
        gst_onnx_meta_transform);
    g_once_init_leave (&onnx_meta_info, meta_info);
  }
  return onnx_meta_info;
}

GstOnnxMeta *
gst_buffer_add_onnx_meta (GstBuffer * buffer, gint x, gint y, gint w, gint h, const gchar * label)
{
  GstOnnxMeta *meta;

  meta = (GstOnnxMeta *) gst_buffer_add_meta (buffer, GST_ONNX_META_INFO, NULL);
  meta->x = x;
  meta->y = y;
  meta->w = w;
  meta->h = h;
  meta->label = g_strdup (label);

  return meta;
}

