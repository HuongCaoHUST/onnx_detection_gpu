#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/base.h>

#include "gstonnxtracker.h"
#include "gstonnxmeta.h"
/* ByteTrack headers pulled in via gstonnxtracker.h → ByteTrack/BYTETracker.h */
#include "ByteTrack/Object.h"
#include "ByteTrack/Rect.h"

#include <vector>
#include <string>
#include <map>

GST_DEBUG_CATEGORY_STATIC (gst_onnxtracker_debug);
#define GST_CAT_DEFAULT gst_onnxtracker_debug

/* ---------------------------------------------------------------------------
 * Pad templates
 * ------------------------------------------------------------------------- */
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

#define gst_onnxtracker_parent_class parent_class
G_DEFINE_TYPE (Gstonnxtracker, gst_onnxtracker, GST_TYPE_BASE_TRANSFORM);

/* ---------------------------------------------------------------------------
 * GEnum for algorithm
 * ------------------------------------------------------------------------- */
GType
gst_onnxtracker_algorithm_get_type (void)
{
  static GType type = 0;
  if (!type) {
    static const GEnumValue values[] = {
      {GST_ONNX_TRACKER_ALGO_IOU,       "IoU Tracker (No Kalman Filter)",                 "iou"},
      {GST_ONNX_TRACKER_ALGO_SORT,      "SORT Tracker (Kalman Filter + IoU)",             "sort"},
      {GST_ONNX_TRACKER_ALGO_BYTETRACK, "ByteTrack (Dual-threshold + Hungarian + KF)",   "bytetrack"},
      {0, NULL, NULL}
    };
    type = g_enum_register_static ("GstOnnxTrackerAlgorithm", values);
  }
  return type;
}

/* ---------------------------------------------------------------------------
 * GObject property IDs
 * ------------------------------------------------------------------------- */
enum
{
  PROP_0,
  PROP_TRACKER_ALGORITHM,
  PROP_BT_FRAME_RATE,
  PROP_BT_TRACK_BUFFER,
  PROP_BT_TRACK_THRESH,
  PROP_BT_HIGH_THRESH,
  PROP_BT_MATCH_THRESH,
};

/* Default values */
#define DEFAULT_TRACKER_ALGORITHM  GST_ONNX_TRACKER_ALGO_SORT
#define DEFAULT_BT_FRAME_RATE      30
#define DEFAULT_BT_TRACK_BUFFER    30
#define DEFAULT_BT_TRACK_THRESH    0.5f
#define DEFAULT_BT_HIGH_THRESH     0.6f
#define DEFAULT_BT_MATCH_THRESH    0.8f

/* ---------------------------------------------------------------------------
 * Forward declarations
 * ------------------------------------------------------------------------- */
static GstFlowReturn gst_onnxtracker_transform_ip (GstBaseTransform * base, GstBuffer * buf);
static void gst_onnxtracker_finalize (GObject * object);
static void gst_onnxtracker_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_onnxtracker_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec);

/* ---------------------------------------------------------------------------
 * Class init
 * ------------------------------------------------------------------------- */
static void
gst_onnxtracker_class_init (GstonnxtrackerClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBaseTransformClass *basetransform_class = (GstBaseTransformClass *) klass;

  gobject_class->finalize     = gst_onnxtracker_finalize;
  gobject_class->set_property = gst_onnxtracker_set_property;
  gobject_class->get_property = gst_onnxtracker_get_property;

  /* --- algorithm selector --- */
  g_object_class_install_property (gobject_class, PROP_TRACKER_ALGORITHM,
      g_param_spec_enum ("tracker-algorithm", "Tracker Algorithm",
          "Algorithm to use for object tracking",
          GST_TYPE_ONNX_TRACKER_ALGORITHM, DEFAULT_TRACKER_ALGORITHM,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  /* --- ByteTrack-specific properties --- */
  g_object_class_install_property (gobject_class, PROP_BT_FRAME_RATE,
      g_param_spec_int ("bt-frame-rate", "BT Frame Rate",
          "[ByteTrack] Assumed frame rate used to scale the track buffer",
          1, 120, DEFAULT_BT_FRAME_RATE,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_BT_TRACK_BUFFER,
      g_param_spec_int ("bt-track-buffer", "BT Track Buffer",
          "[ByteTrack] Number of frames to retain a lost track before removal",
          1, 300, DEFAULT_BT_TRACK_BUFFER,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_BT_TRACK_THRESH,
      g_param_spec_float ("bt-track-thresh", "BT Track Threshold",
          "[ByteTrack] Min detection score to enter the high-score candidate set",
          0.0f, 1.0f, DEFAULT_BT_TRACK_THRESH,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_BT_HIGH_THRESH,
      g_param_spec_float ("bt-high-thresh", "BT High Threshold",
          "[ByteTrack] Min detection score required to initialise a brand-new track",
          0.0f, 1.0f, DEFAULT_BT_HIGH_THRESH,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_BT_MATCH_THRESH,
      g_param_spec_float ("bt-match-thresh", "BT Match Threshold",
          "[ByteTrack] Maximum IoU distance used in the first assignment stage",
          0.0f, 1.0f, DEFAULT_BT_MATCH_THRESH,
          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_details_simple (gstelement_class,
      "ONNX Tracker", "Filter/Video",
      "Tracks detections using IoU / SORT / ByteTrack algorithms",
      "HuongCao <<user@hostname.org>>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));

  basetransform_class->transform_ip = GST_DEBUG_FUNCPTR (gst_onnxtracker_transform_ip);

  GST_DEBUG_CATEGORY_INIT (gst_onnxtracker_debug, "onnxtracker", 0, "onnxtracker element");
}

/* ---------------------------------------------------------------------------
 * Instance init
 * ------------------------------------------------------------------------- */
static void
gst_onnxtracker_init (Gstonnxtracker * filter)
{
  filter->next_track_id    = 0;
  filter->active_tracks    = new std::map<int, Track>();
  filter->tracker_algorithm = DEFAULT_TRACKER_ALGORITHM;
  filter->bytetracker      = nullptr;
  filter->bt_frame_rate    = DEFAULT_BT_FRAME_RATE;
  filter->bt_track_buffer  = DEFAULT_BT_TRACK_BUFFER;
  filter->bt_track_thresh  = DEFAULT_BT_TRACK_THRESH;
  filter->bt_high_thresh   = DEFAULT_BT_HIGH_THRESH;
  filter->bt_match_thresh  = DEFAULT_BT_MATCH_THRESH;

  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (filter), TRUE);
}

/* ---------------------------------------------------------------------------
 * Helper – (re)create BYTETracker with current parameters
 * ------------------------------------------------------------------------- */
static void
recreate_bytetracker (Gstonnxtracker * filter)
{
  delete filter->bytetracker;
  filter->bytetracker = new byte_track::BYTETracker (
      filter->bt_frame_rate,
      filter->bt_track_buffer,
      filter->bt_track_thresh,
      filter->bt_high_thresh,
      filter->bt_match_thresh);
  GST_DEBUG_OBJECT (filter,
      "ByteTracker (re)created: fps=%d buf=%d thr=%.2f/%.2f/%.2f",
      filter->bt_frame_rate, filter->bt_track_buffer,
      filter->bt_track_thresh, filter->bt_high_thresh, filter->bt_match_thresh);
}

/* ---------------------------------------------------------------------------
 * GObject set_property
 * ------------------------------------------------------------------------- */
static void
gst_onnxtracker_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  Gstonnxtracker *filter = GST_ONNXTRACKER (object);

  switch (prop_id) {
    case PROP_TRACKER_ALGORITHM:
      filter->tracker_algorithm = (GstOnnxTrackerAlgorithm) g_value_get_enum (value);
      /* Lazily create BYTETracker when the algorithm is selected */
      if (filter->tracker_algorithm == GST_ONNX_TRACKER_ALGO_BYTETRACK)
        recreate_bytetracker (filter);
      break;
    case PROP_BT_FRAME_RATE:
      filter->bt_frame_rate = g_value_get_int (value);
      if (filter->bytetracker) recreate_bytetracker (filter);
      break;
    case PROP_BT_TRACK_BUFFER:
      filter->bt_track_buffer = g_value_get_int (value);
      if (filter->bytetracker) recreate_bytetracker (filter);
      break;
    case PROP_BT_TRACK_THRESH:
      filter->bt_track_thresh = g_value_get_float (value);
      if (filter->bytetracker) recreate_bytetracker (filter);
      break;
    case PROP_BT_HIGH_THRESH:
      filter->bt_high_thresh = g_value_get_float (value);
      if (filter->bytetracker) recreate_bytetracker (filter);
      break;
    case PROP_BT_MATCH_THRESH:
      filter->bt_match_thresh = g_value_get_float (value);
      if (filter->bytetracker) recreate_bytetracker (filter);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* ---------------------------------------------------------------------------
 * GObject get_property
 * ------------------------------------------------------------------------- */
static void
gst_onnxtracker_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  Gstonnxtracker *filter = GST_ONNXTRACKER (object);

  switch (prop_id) {
    case PROP_TRACKER_ALGORITHM:
      g_value_set_enum (value, filter->tracker_algorithm);
      break;
    case PROP_BT_FRAME_RATE:
      g_value_set_int (value, filter->bt_frame_rate);
      break;
    case PROP_BT_TRACK_BUFFER:
      g_value_set_int (value, filter->bt_track_buffer);
      break;
    case PROP_BT_TRACK_THRESH:
      g_value_set_float (value, filter->bt_track_thresh);
      break;
    case PROP_BT_HIGH_THRESH:
      g_value_set_float (value, filter->bt_high_thresh);
      break;
    case PROP_BT_MATCH_THRESH:
      g_value_set_float (value, filter->bt_match_thresh);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* ---------------------------------------------------------------------------
 * GObject finalize
 * ------------------------------------------------------------------------- */
static void
gst_onnxtracker_finalize (GObject * object)
{
  Gstonnxtracker *filter = GST_ONNXTRACKER (object);
  delete filter->active_tracks;
  delete filter->bytetracker;
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* ===========================================================================
 * Utility functions used by IOU / SORT algorithms
 * ========================================================================= */

static float compute_iou(const cv::Rect& a, const cv::Rect& b) {
  int inter_x1 = std::max(a.x, b.x);
  int inter_y1 = std::max(a.y, b.y);
  int inter_x2 = std::min(a.x + a.width,  b.x + b.width);
  int inter_y2 = std::min(a.y + a.height, b.y + b.height);

  int inter_w = std::max(0, inter_x2 - inter_x1);
  int inter_h = std::max(0, inter_y2 - inter_y1);

  float inter_area  = (float)(inter_w * inter_h);
  float union_area  = (float)(a.area() + b.area()) - inter_area;
  if (union_area <= 0) return 0.0f;
  return inter_area / union_area;
}

static void init_kf(cv::KalmanFilter& kf, const cv::Rect& rect) {
  kf.init(4, 2, 0); /* state: [cx, cy, dx, dy], measurement: [cx, cy] */
  kf.transitionMatrix = (cv::Mat_<float>(4, 4) << 1, 0, 1, 0,
                                                   0, 1, 0, 1,
                                                   0, 0, 1, 0,
                                                   0, 0, 0, 1);
  cv::setIdentity(kf.measurementMatrix);
  cv::setIdentity(kf.processNoiseCov,     cv::Scalar::all(1e-2));
  cv::setIdentity(kf.measurementNoiseCov, cv::Scalar::all(1e-1));
  cv::setIdentity(kf.errorCovPost,        cv::Scalar::all(1));

  float cx = rect.x + rect.width  / 2.0f;
  float cy = rect.y + rect.height / 2.0f;
  kf.statePost = (cv::Mat_<float>(4, 1) << cx, cy, 0, 0);
  kf.statePre  = kf.statePost.clone();
}

/* ===========================================================================
 * Core transform_ip
 * ========================================================================= */

static GstFlowReturn
gst_onnxtracker_transform_ip (GstBaseTransform * base, GstBuffer * buf)
{
  Gstonnxtracker *filter = GST_ONNXTRACKER (base);

  /* -----------------------------------------------------------------------
   * 1. Collect all incoming detection metadata from the buffer
   * --------------------------------------------------------------------- */
  std::vector<GstOnnxMeta*> detections;
  gpointer state = NULL;
  GstMeta *meta;
  while ((meta = gst_buffer_iterate_meta (buf, &state))) {
    if (meta->info->api == GST_ONNX_META_API_TYPE) {
      detections.push_back ((GstOnnxMeta *) meta);
    }
  }

  GST_DEBUG_OBJECT (filter,
      "Received %zu detections, algorithm=%d",
      detections.size(), (int)filter->tracker_algorithm);

  /* ===================================================================
   * BYTETRACK path
   * ================================================================= */
  if (filter->tracker_algorithm == GST_ONNX_TRACKER_ALGO_BYTETRACK) {

    /* Lazy-init the tracker (may happen if set_property was never called
       with the BYTETRACK enum before the first buffer arrives). */
    if (!filter->bytetracker)
      recreate_bytetracker (filter);

    /* -- Build a vector of byte_track::Object from GstOnnxMeta -- */
    std::vector<byte_track::Object> objects;
    objects.reserve (detections.size());

    for (GstOnnxMeta *d : detections) {
      /* ByteTrack uses tlwh (top-left x, top-left y, width, height) */
      byte_track::Rect<float> rect (
          static_cast<float>(d->x),
          static_cast<float>(d->y),
          static_cast<float>(d->w),
          static_cast<float>(d->h));

      /* Convert string label to integer class index.
       * We use a simple static map built on first use. */
      static std::map<std::string, int> label_to_id;
      static int next_label_id = 0;
      int label_id = 0;
      if (d->label) {
        std::string lbl (d->label);
        auto it = label_to_id.find (lbl);
        if (it == label_to_id.end()) {
          label_id = next_label_id;
          label_to_id[lbl] = next_label_id++;
        } else {
          label_id = it->second;
        }
      }

      objects.emplace_back (rect, label_id, d->score);
    }

    /* -- Run ByteTrack update -- */
    std::vector<byte_track::BYTETracker::STrackPtr> tracked = filter->bytetracker->update (objects);

    GST_DEBUG_OBJECT (filter, "ByteTrack returned %zu active tracks", tracked.size());

    /* -- Match output tracks back to GstOnnxMeta by best IoU overlap --
     *
     * ByteTracker may smooth/shift boxes via Kalman prediction, so we
     * match the STrack rect to the original detection rect using IoU,
     * keeping a 1-to-1 assignment (greedy, best-first).
     */
    std::vector<bool> track_used (tracked.size(), false);

    for (GstOnnxMeta *d : detections) {
      cv::Rect det_box (d->x, d->y, d->w, d->h);
      float best_iou = 0.0f;
      int   best_ti  = -1;

      for (size_t ti = 0; ti < tracked.size(); ++ti) {
        if (track_used[ti]) continue;
        const auto &r = tracked[ti]->getRect();
        cv::Rect trk_box (
            static_cast<int>(r.x()),
            static_cast<int>(r.y()),
            static_cast<int>(r.width()),
            static_cast<int>(r.height()));
        float iou = compute_iou (det_box, trk_box);
        if (iou > best_iou) {
          best_iou = iou;
          best_ti  = (int)ti;
        }
      }

      if (best_ti >= 0 && best_iou > 0.3f) {
        d->track_id = (gint) tracked[best_ti]->getTrackId();
        track_used[best_ti] = true;
      } else {
        /* Detection not matched to any confirmed track – mark as untracked */
        d->track_id = -1;
      }
    }

    return GST_FLOW_OK;
  }

  /* ===================================================================
   * IOU / SORT path  (original implementation, unchanged)
   * ================================================================= */

  /* 2. Predict step for all active tracks */
  for (auto& pair : *filter->active_tracks) {
    Track& t = pair.second;
    if (filter->tracker_algorithm == GST_ONNX_TRACKER_ALGO_SORT) {
      cv::Mat prediction = t.kf.predict();
      float cx = prediction.at<float>(0);
      float cy = prediction.at<float>(1);

      t.predicted_box = cv::Rect(
          static_cast<int>(cx - t.predicted_box.width  / 2.0f),
          static_cast<int>(cy - t.predicted_box.height / 2.0f),
          t.predicted_box.width,
          t.predicted_box.height);
    } else {
      t.predicted_box = t.last_box;
    }
  }

  /* 3. Greedy matching */
  std::vector<bool> det_matched (detections.size(), false);
  std::vector<int>  det_to_track (detections.size(), -1);

  for (auto& pair : *filter->active_tracks) {
    Track& t = pair.second;
    float best_iou    = 0.0f;
    int   best_det_idx = -1;

    for (size_t i = 0; i < detections.size(); ++i) {
      if (det_matched[i]) continue;
      if (t.label != detections[i]->label) continue;

      cv::Rect det_box (detections[i]->x, detections[i]->y,
                        detections[i]->w, detections[i]->h);
      float iou = compute_iou (t.predicted_box, det_box);
      if (iou > best_iou) {
        best_iou    = iou;
        best_det_idx = i;
      }
    }

    if (best_det_idx != -1 && best_iou >= 0.3f) {
      det_matched[best_det_idx] = true;
      det_to_track[best_det_idx] = t.track_id;

      cv::Rect best_box (detections[best_det_idx]->x, detections[best_det_idx]->y,
                         detections[best_det_idx]->w, detections[best_det_idx]->h);
      if (filter->tracker_algorithm == GST_ONNX_TRACKER_ALGO_SORT) {
        float cx = best_box.x + best_box.width  / 2.0f;
        float cy = best_box.y + best_box.height / 2.0f;
        cv::Mat measurement = (cv::Mat_<float>(2, 1) << cx, cy);
        t.kf.correct (measurement);
      }
      t.predicted_box   = best_box;
      t.last_box        = best_box;
      t.missed_frames   = 0;
    } else {
      t.missed_frames++;
    }
  }

  /* 4. Handle detections – assign existing or create new tracks */
  for (size_t i = 0; i < detections.size(); ++i) {
    if (det_matched[i]) {
      detections[i]->track_id = det_to_track[i];
    } else {
      int new_id = filter->next_track_id++;
      detections[i]->track_id = new_id;

      cv::Rect new_box (detections[i]->x, detections[i]->y,
                        detections[i]->w, detections[i]->h);
      Track t;
      t.track_id      = new_id;
      t.missed_frames = 0;
      t.predicted_box = new_box;
      t.last_box      = new_box;
      t.label         = detections[i]->label ? detections[i]->label : "";

      if (filter->tracker_algorithm == GST_ONNX_TRACKER_ALGO_SORT)
        init_kf (t.kf, new_box);

      (*filter->active_tracks)[new_id] = std::move (t);
    }
  }

  /* 5. Remove stale tracks (missed for ≥30 frames) */
  for (auto it = filter->active_tracks->begin(); it != filter->active_tracks->end(); ) {
    if (it->second.missed_frames >= 30)
      it = filter->active_tracks->erase (it);
    else
      ++it;
  }

  return GST_FLOW_OK;
}

/* ---------------------------------------------------------------------------
 * GStreamer plugin registration
 * ------------------------------------------------------------------------- */
static gboolean
onnxtracker_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "onnxtracker", GST_RANK_NONE,
      GST_TYPE_ONNXTRACKER);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    onnxtracker,
    "Multi-algorithm object tracker (IoU / SORT / ByteTrack)",
    onnxtracker_init,
    PACKAGE_VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
