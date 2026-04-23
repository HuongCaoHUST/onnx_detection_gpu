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
#include <map>

G_BEGIN_DECLS

typedef enum {
  GST_ONNXOVERLAY_MC_NONE = 0,
  GST_ONNXOVERLAY_MC_FORWARD = 1,
  GST_ONNXOVERLAY_MC_LINEAR = 2
} GstOnnxOverlayMCMethod;

#define GST_TYPE_ONNXOVERLAY_MC_METHOD (gst_onnxoverlay_mc_method_get_type())
GType gst_onnxoverlay_mc_method_get_type (void);

struct TrackVelocityState {
    float last_x, last_y, last_w, last_h;
    float prev_x, prev_y, prev_w, prev_h;
    float dx, dy, dw, dh;
    GstClockTime last_pts;
};



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

  // Cache: giữ lại metadata của lần inference gần nhất
  // để dùng lại khi inference chưa có kết quả mới (tránh bbox nhấp nháy)
  GstBuffer *last_meta_buf;
  
  GstOnnxOverlayMCMethod mc_method;
  std::map<int, TrackVelocityState> *track_states;
};

G_END_DECLS

#endif /* __GST_ONNXOVERLAY_H__ */
