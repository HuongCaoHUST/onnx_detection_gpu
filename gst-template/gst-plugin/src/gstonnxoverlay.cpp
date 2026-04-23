/*
 * GStreamer
 * Copyright (C) 2026 HuongCao <<user@hostname.org>>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <opencv2/opencv.hpp>
#include "gstonnxoverlay.h"
#include "gstonnxmeta.h"

#include <queue>
#include <mutex>

GST_DEBUG_CATEGORY_STATIC (gst_onnxoverlay_debug);
#define GST_CAT_DEFAULT gst_onnxoverlay_debug
 
enum
{
  PROP_0,
  PROP_USE_MOTION_COMPENSATION,
};


// Pad templates
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, format=(string){ RGB, BGR, RGBx, BGRx }")
    );

static GstStaticPadTemplate sink_meta_template = GST_STATIC_PAD_TEMPLATE ("sink_meta",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("ANY")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, format=(string){ RGB, BGR, RGBx, BGRx }")
    );

#define gst_onnxoverlay_parent_class parent_class
G_DEFINE_TYPE (Gstonnxoverlay, gst_onnxoverlay, GST_TYPE_ELEMENT);

// Forward declarations
static GstFlowReturn gst_onnxoverlay_sink_chain (GstPad * pad, GstObject * parent, GstBuffer * buf);
static GstFlowReturn gst_onnxoverlay_sink_meta_chain (GstPad * pad, GstObject * parent, GstBuffer * buf);
static gboolean gst_onnxoverlay_sink_event (GstPad * pad, GstObject * parent, GstEvent * event);
static gboolean gst_onnxoverlay_src_event (GstPad * pad, GstObject * parent, GstEvent * event);
static GstPad * gst_onnxoverlay_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps);
static void gst_onnxoverlay_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_onnxoverlay_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_onnxoverlay_finalize (GObject * object);

GType
gst_onnxoverlay_mc_method_get_type (void)
{
  static GType type = 0;
  if (!type) {
    static const GEnumValue values[] = {
      { GST_ONNXOVERLAY_MC_NONE, "None (disabled)", "none" },
      { GST_ONNXOVERLAY_MC_NONE, "None (false)", "false" },
      { GST_ONNXOVERLAY_MC_FORWARD, "Forward-fill", "forward" },
      { GST_ONNXOVERLAY_MC_FORWARD, "Forward-fill (true)", "true" },
      { GST_ONNXOVERLAY_MC_LINEAR, "Linear Interpolation", "linear" },
      { 0, NULL, NULL }
    };
    type = g_enum_register_static ("GstOnnxOverlayMCMethod", values);
  }
  return type;
}

static void
gst_onnxoverlay_class_init (GstonnxoverlayClass * klass)
{
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  gst_element_class_add_static_pad_template (gstelement_class, &sink_template);
  gst_element_class_add_static_pad_template (gstelement_class, &sink_meta_template);
  gst_element_class_add_static_pad_template (gstelement_class, &src_template);

  gst_element_class_set_details_simple (gstelement_class,
      "ONNX Overlay", "Filter/Editor/Video",
      "Overlays detection metadata on video (non-blocking)", "HuongCao <<user@hostname.org>>");

  GObjectClass *gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_onnxoverlay_set_property;
  gobject_class->get_property = gst_onnxoverlay_get_property;
  gobject_class->finalize = gst_onnxoverlay_finalize;

  g_object_class_install_property (gobject_class, PROP_USE_MOTION_COMPENSATION,
      g_param_spec_enum ("motion-compensation", "Motion Compensation Method",
          "Select the motion compensation method for intermediate frames",
          GST_TYPE_ONNXOVERLAY_MC_METHOD,
          GST_ONNXOVERLAY_MC_FORWARD,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gstelement_class->request_new_pad = gst_onnxoverlay_request_new_pad;

  GST_DEBUG_CATEGORY_INIT (gst_onnxoverlay_debug, "onnxoverlay", 0, "ONNX Overlay");
}

static void
gst_onnxoverlay_init (Gstonnxoverlay * filter)
{
  filter->sink_pad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_chain_function (filter->sink_pad, gst_onnxoverlay_sink_chain);
  gst_pad_set_event_function (filter->sink_pad, gst_onnxoverlay_sink_event);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sink_pad);

  // Meta pad created on request
  filter->sink_meta_pad = NULL;

  filter->src_pad = gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_set_event_function (filter->src_pad, gst_onnxoverlay_src_event);
  gst_element_add_pad (GST_ELEMENT (filter), filter->src_pad);

  // Initialize metadata queue
  filter->meta_queue = new std::queue<GstBuffer*>();
  filter->meta_queue_lock = new std::mutex();
  filter->meta_thread = NULL;
  filter->stop_thread = FALSE;
  filter->last_meta_buf = NULL;
  filter->mc_method = GST_ONNXOVERLAY_MC_FORWARD;
  filter->track_states = new std::map<int, TrackVelocityState>();
}

static void
gst_onnxoverlay_finalize (GObject * object)
{
  Gstonnxoverlay *filter = GST_ONNXOVERLAY (object);
  delete filter->meta_queue;
  delete filter->meta_queue_lock;
  delete filter->track_states;
  if (filter->last_meta_buf) {
    gst_buffer_unref (filter->last_meta_buf);
  }
  G_OBJECT_CLASS (gst_onnxoverlay_parent_class)->finalize (object);
}

static GstPad *
gst_onnxoverlay_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps)
{
  Gstonnxoverlay *filter = GST_ONNXOVERLAY (element);

  if (templ == gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (element), "sink_meta")) {
    if (!filter->sink_meta_pad) {
      filter->sink_meta_pad = gst_pad_new_from_template (templ, "sink_meta");
      gst_pad_set_chain_function (filter->sink_meta_pad, gst_onnxoverlay_sink_meta_chain);
      gst_pad_set_event_function (filter->sink_meta_pad, gst_onnxoverlay_sink_event);
      gst_element_add_pad (element, filter->sink_meta_pad);
      return filter->sink_meta_pad;
    }
  }
  return NULL;
}

static void
gst_onnxoverlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  Gstonnxoverlay *filter = GST_ONNXOVERLAY (object);

  switch (prop_id) {
    case PROP_USE_MOTION_COMPENSATION:
      filter->mc_method = (GstOnnxOverlayMCMethod) g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_onnxoverlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  Gstonnxoverlay *filter = GST_ONNXOVERLAY (object);

  switch (prop_id) {
    case PROP_USE_MOTION_COMPENSATION:
      g_value_set_enum (value, filter->mc_method);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstFlowReturn
gst_onnxoverlay_sink_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  Gstonnxoverlay *filter = GST_ONNXOVERLAY (parent);
  GstBuffer *meta_buf = NULL;

  bool is_new_meta = false;

  // Check if there's a metadata buffer in queue (non-blocking)
  {
    std::lock_guard<std::mutex> lock(*filter->meta_queue_lock);
    if (!filter->meta_queue->empty()) {
      meta_buf = filter->meta_queue->front();
      filter->meta_queue->pop();

      if (filter->last_meta_buf)
        gst_buffer_unref (filter->last_meta_buf);
      filter->last_meta_buf = gst_buffer_ref (meta_buf);
      
      is_new_meta = true;
    } else if (filter->mc_method != GST_ONNXOVERLAY_MC_NONE && filter->last_meta_buf) {
      meta_buf = gst_buffer_ref (filter->last_meta_buf);
      GST_DEBUG_OBJECT (filter, "No new metadata, reusing cached bounding boxes");
    }
  }

  // Get video caps
  GstCaps *caps = gst_pad_get_current_caps (pad);
  if (caps) {
    GstStructure *s = gst_caps_get_structure (caps, 0);
    int width = 0, height = 0;
    const gchar *format = gst_structure_get_string (s, "format");

    if (gst_structure_get_int (s, "width", &width) &&
        gst_structure_get_int (s, "height", &height)) {

      GstMapInfo map;
      buf = gst_buffer_make_writable (buf);

      if (gst_buffer_map (buf, &map, (GstMapFlags)GST_MAP_READWRITE)) {
        int channels = (g_str_has_suffix (format, "x")) ? 4 : 3;
        cv::Mat img(height, width, (channels == 4) ? CV_8UC4 : CV_8UC3, map.data);

        if (meta_buf) {
          GstMeta *meta;
          gpointer state = NULL;

          double scale_x = (double)width / 640.0;
          double scale_y = (double)height / 640.0;

          GST_DEBUG_OBJECT (filter, "Overlaying %d metadata items on video frame",
              gst_buffer_n_memory (meta_buf));

          while ((meta = gst_buffer_iterate_meta (meta_buf, &state))) {
            if (meta->info->api == GST_ONNX_META_API_TYPE) {
              GstOnnxMeta *ometa = (GstOnnxMeta *) meta;

              float raw_x = ometa->x;
              float raw_y = ometa->y;
              float raw_w = ometa->w;
              float raw_h = ometa->h;
              int track_id = ometa->track_id;

              if (track_id >= 0) {
                GstClockTime buf_pts = GST_BUFFER_PTS(buf);
                GstClockTime meta_pts = ometa->pts;

                if (is_new_meta) {
                  if (filter->track_states->count(track_id)) {
                    auto& ts = (*filter->track_states)[track_id];
                    if (GST_CLOCK_TIME_IS_VALID(meta_pts) && GST_CLOCK_TIME_IS_VALID(ts.last_pts) && meta_pts > ts.last_pts) {
                        double dt_ns = (double)(meta_pts - ts.last_pts);
                        ts.dx = (raw_x - ts.last_x) / dt_ns;
                        ts.dy = (raw_y - ts.last_y) / dt_ns;
                        ts.dw = (raw_w - ts.last_w) / dt_ns;
                        ts.dh = (raw_h - ts.last_h) / dt_ns;
                    }
                  } else {
                    (*filter->track_states)[track_id] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, GST_CLOCK_TIME_NONE};
                  }
                  auto& ts = (*filter->track_states)[track_id];
                  // Lưu điểm cũ để clamp
                  ts.prev_x = ts.last_x;
                  ts.prev_y = ts.last_y;
                  ts.prev_w = ts.last_w;
                  ts.prev_h = ts.last_h;
                  
                  ts.last_x = raw_x;
                  ts.last_y = raw_y;
                  ts.last_w = raw_w;
                  ts.last_h = raw_h;
                  ts.last_pts = meta_pts;
                }
                
                if (filter->mc_method == GST_ONNXOVERLAY_MC_LINEAR && filter->track_states->count(track_id)) {
                  auto& ts = (*filter->track_states)[track_id];
                  if (GST_CLOCK_TIME_IS_VALID(buf_pts) && GST_CLOCK_TIME_IS_VALID(ts.last_pts)) {
                      double time_diff = (double)buf_pts - (double)ts.last_pts;
                      // Không giới hạn >0 để nội suy lùi được về quá khứ (nếu meta nằm ở tương lai so với màn hình)
                      raw_x = ts.last_x + ts.dx * time_diff;
                      raw_y = ts.last_y + ts.dy * time_diff;
                      raw_w = ts.last_w + ts.dw * time_diff;
                      raw_h = ts.last_h + ts.dh * time_diff;
                      
                      // Xác nhận 100% kẹp chặt tọa độ giữa 2 box cũ và mới
                      if (ts.prev_x != 0 || ts.prev_y != 0) {
                          float min_x = std::min(ts.prev_x, ts.last_x);
                          float max_x = std::max(ts.prev_x, ts.last_x);
                          float min_y = std::min(ts.prev_y, ts.last_y);
                          float max_y = std::max(ts.prev_y, ts.last_y);
                          
                          raw_x = std::min(std::max((float)raw_x, min_x), max_x);
                          raw_y = std::min(std::max((float)raw_y, min_y), max_y);
                      }
                  } else {
                      raw_x = ts.last_x;
                      raw_y = ts.last_y;
                      raw_w = ts.last_w;
                      raw_h = ts.last_h;
                  }
                } else if (filter->mc_method != GST_ONNXOVERLAY_MC_NONE && filter->track_states->count(track_id)) {
                  auto& ts = (*filter->track_states)[track_id];
                  raw_x = ts.last_x;
                  raw_y = ts.last_y;
                  raw_w = ts.last_w;
                  raw_h = ts.last_h;
                }
              }

              int x = (int)(raw_x * scale_x);
              int y = (int)(raw_y * scale_y);
              int w = (int)(raw_w * scale_x);
              int h = (int)(raw_h * scale_y);

              cv::rectangle(img, cv::Rect(x, y, w, h), cv::Scalar(0, 255, 0), 2);
              if (ometa->label) {
                std::string display_label = ometa->label;
                GST_DEBUG_OBJECT (filter, "Drawing label %s with track_id %d", ometa->label, ometa->track_id);
                if (ometa->track_id >= 0) {
                  display_label += " #" + std::to_string(ometa->track_id);
                }
                cv::putText(img, display_label, cv::Point(x, y - 5),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 1);
              }
            }
          }
        } else {
          GST_DEBUG_OBJECT (filter, "No metadata available, outputting video as-is");
        }
        gst_buffer_unmap (buf, &map);
      }
    }
    gst_caps_unref (caps);
  }

  if (meta_buf) {
    gst_buffer_unref (meta_buf);
  }

  return gst_pad_push (filter->src_pad, buf);
}

static GstFlowReturn
gst_onnxoverlay_sink_meta_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  Gstonnxoverlay *filter = GST_ONNXOVERLAY (parent);

  // Just queue the metadata buffer, don't block
  {
    std::lock_guard<std::mutex> lock(*filter->meta_queue_lock);
    filter->meta_queue->push (gst_buffer_ref (buf));

    // Keep queue size small (max 5)
    while (filter->meta_queue->size() > 5) {
      GstBuffer *old = filter->meta_queue->front();
      filter->meta_queue->pop();
      gst_buffer_unref (old);
    }
  }

  gst_buffer_unref (buf);
  return GST_FLOW_OK;
}

static gboolean
gst_onnxoverlay_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  Gstonnxoverlay *filter = GST_ONNXOVERLAY (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      // Clear metadata queue on flush
      {
        std::lock_guard<std::mutex> lock(*filter->meta_queue_lock);
        while (!filter->meta_queue->empty()) {
          GstBuffer *buf = filter->meta_queue->front();
          filter->meta_queue->pop();
          gst_buffer_unref (buf);
        }
        // Xóa cả cache khi flush (ví dụ: seek, restart)
        if (filter->last_meta_buf) {
          gst_buffer_unref (filter->last_meta_buf);
          filter->last_meta_buf = NULL;
        }
      }
      break;
    default:
      break;
  }

  // Forward event from sink to src
  if (pad == filter->sink_pad) {
    return gst_pad_push_event (filter->src_pad, event);
  } else {
    gst_event_unref (event);
    return TRUE;
  }
}

static gboolean
gst_onnxoverlay_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  Gstonnxoverlay *filter = GST_ONNXOVERLAY (parent);

  // Forward src events to sink
  return gst_pad_push_event (filter->sink_pad, event);
}

static gboolean
onnxoverlay_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "onnxoverlay", GST_RANK_NONE,
      GST_TYPE_ONNXOVERLAY);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    onnxoverlay,
    "Merges custom ONNX metadata with original video (non-blocking)",
    onnxoverlay_init,
    PACKAGE_VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
