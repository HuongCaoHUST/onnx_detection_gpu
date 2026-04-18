/*
 * GStreamer
 * Copyright (C) 2026 HuongCao <<user@hostname.org>>
 */

#ifndef __GST_ONNXOVERLAY_H__
#define __GST_ONNXOVERLAY_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <queue>
#include <mutex>

G_BEGIN_DECLS

#define GST_TYPE_ONNXOVERLAY (gst_onnxoverlay_get_type())
G_DECLARE_FINAL_TYPE (Gstonnxoverlay, gst_onnxoverlay,
    GST, ONNXOVERLAY, GstElement)

struct _Gstonnxoverlay {
  GstElement element;

  GstPad *sink_pad;        // Video input (main)
  GstPad *sink_meta_pad;   // Metadata input (secondary, non-blocking)
  GstPad *src_pad;         // Video output

  // Metadata queue for async handling
  std::queue<GstBuffer*> *meta_queue;
  std::mutex *meta_queue_lock;
  GThread *meta_thread;
  gboolean stop_thread;
};

G_END_DECLS

#endif /* __GST_ONNXOVERLAY_H__ */
