#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/base.h>

#include "gstonnxtracker.h"
#include "gstonnxmeta.h"
#include <vector>

GST_DEBUG_CATEGORY_STATIC (gst_onnxtracker_debug);
#define GST_CAT_DEFAULT gst_onnxtracker_debug

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

static GstFlowReturn gst_onnxtracker_transform_ip (GstBaseTransform * base, GstBuffer * buf);
static void gst_onnxtracker_finalize (GObject * object);

static void
gst_onnxtracker_class_init (GstonnxtrackerClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBaseTransformClass *basetransform_class = (GstBaseTransformClass *) klass;

  gobject_class->finalize = gst_onnxtracker_finalize;

  gst_element_class_set_details_simple (gstelement_class,
      "ONNX Tracker", "Filter/Video",
      "Tracks detections using Kalman Filter and IoU", "HuongCao <<user@hostname.org>>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));

  basetransform_class->transform_ip = GST_DEBUG_FUNCPTR (gst_onnxtracker_transform_ip);

  GST_DEBUG_CATEGORY_INIT (gst_onnxtracker_debug, "onnxtracker", 0, "onnxtracker element");
}

static void
gst_onnxtracker_init (Gstonnxtracker * filter)
{
  filter->next_track_id = 0;
  filter->active_tracks = new std::map<int, Track>();
  // Make sure it runs in place
  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (filter), TRUE);
}

static void
gst_onnxtracker_finalize (GObject * object)
{
  Gstonnxtracker *filter = GST_ONNXTRACKER (object);
  delete filter->active_tracks;
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static float compute_iou(const cv::Rect& a, const cv::Rect& b) {
  int inter_x1 = std::max(a.x, b.x);
  int inter_y1 = std::max(a.y, b.y);
  int inter_x2 = std::min(a.x + a.width, b.x + b.width);
  int inter_y2 = std::min(a.y + a.height, b.y + b.height);
  
  int inter_w = std::max(0, inter_x2 - inter_x1);
  int inter_h = std::max(0, inter_y2 - inter_y1);
  
  float inter_area = (float)(inter_w * inter_h);
  float union_area = (float)(a.area() + b.area()) - inter_area;
  if (union_area <= 0) return 0.0f;
  return inter_area / union_area;
}

static void init_kf(cv::KalmanFilter& kf, const cv::Rect& rect) {
  kf.init(4, 2, 0); // state: [x,y,dx,dy], measurement: [x,y]
  kf.transitionMatrix = (cv::Mat_<float>(4, 4) << 1, 0, 1, 0,
                                                  0, 1, 0, 1,
                                                  0, 0, 1, 0,
                                                  0, 0, 0, 1);
  cv::setIdentity(kf.measurementMatrix);
  cv::setIdentity(kf.processNoiseCov, cv::Scalar::all(1e-2));
  cv::setIdentity(kf.measurementNoiseCov, cv::Scalar::all(1e-1));
  cv::setIdentity(kf.errorCovPost, cv::Scalar::all(1));
  
  float cx = rect.x + rect.width / 2.0f;
  float cy = rect.y + rect.height / 2.0f;
  kf.statePost = (cv::Mat_<float>(4, 1) << cx, cy, 0, 0);
  kf.statePre = kf.statePost.clone();
}

static GstFlowReturn
gst_onnxtracker_transform_ip (GstBaseTransform * base, GstBuffer * buf)
{
  Gstonnxtracker *filter = GST_ONNXTRACKER (base);
  
  // 1. Collect all incoming boxes from metadata
  std::vector<GstOnnxMeta*> detections;
  gpointer state = NULL;
  GstMeta *meta;
  while ((meta = gst_buffer_iterate_meta (buf, &state))) {
    if (meta->info->api == GST_ONNX_META_API_TYPE) {
      detections.push_back((GstOnnxMeta *) meta);
    }
  }

  GST_DEBUG_OBJECT (filter, "Received %zu detections, tracking %zu active tracks", detections.size(), filter->active_tracks->size());

  // 2. Predict step for all active tracks
  for (auto& pair : *filter->active_tracks) {
    Track& t = pair.second;
    cv::Mat prediction = t.kf.predict();
    float cx = prediction.at<float>(0);
    float cy = prediction.at<float>(1);
    
    // We assume w, h don't change drastically for IoU matching (could add w,h to state, but simplify for now)
    t.predicted_box = cv::Rect(
        static_cast<int>(cx - t.predicted_box.width / 2.0f),
        static_cast<int>(cy - t.predicted_box.height / 2.0f),
        t.predicted_box.width, 
        t.predicted_box.height
    );
  }

  // 3. Greedy Matching
  std::vector<bool> det_matched(detections.size(), false);
  std::vector<int> det_to_track(detections.size(), -1);
  
  for (auto& pair : *filter->active_tracks) {
    Track& t = pair.second;
    float best_iou = 0.0f;
    int best_det_idx = -1;
    
    for (size_t i = 0; i < detections.size(); ++i) {
      if (det_matched[i]) continue;
      
      // Match label too if possible
      if (t.label != detections[i]->label) continue;
      
      cv::Rect det_box(detections[i]->x, detections[i]->y, detections[i]->w, detections[i]->h);
      float iou = compute_iou(t.predicted_box, det_box);
      if (iou > best_iou) {
        best_iou = iou;
        best_det_idx = i;
      }
    }
    
    if (best_det_idx != -1 && best_iou >= 0.3f) {
      det_matched[best_det_idx] = true;
      det_to_track[best_det_idx] = t.track_id;
      
      // Update KF
      cv::Rect best_box(detections[best_det_idx]->x, detections[best_det_idx]->y, detections[best_det_idx]->w, detections[best_det_idx]->h);
      float cx = best_box.x + best_box.width / 2.0f;
      float cy = best_box.y + best_box.height / 2.0f;
      cv::Mat measurement = (cv::Mat_<float>(2, 1) << cx, cy);
      
      t.kf.correct(measurement);
      t.predicted_box = best_box; // update width/height
      t.missed_frames = 0;
    } else {
      t.missed_frames++;
    }
  }

  // 4. Handle Detections
  for (size_t i = 0; i < detections.size(); ++i) {
    if (det_matched[i]) {
      detections[i]->track_id = det_to_track[i];
    } else {
      // New Track
      int new_id = filter->next_track_id++;
      detections[i]->track_id = new_id;
      
      cv::Rect new_box(detections[i]->x, detections[i]->y, detections[i]->w, detections[i]->h);
      Track t;
      t.track_id = new_id;
      t.missed_frames = 0;
      t.predicted_box = new_box;
      t.label = detections[i]->label ? detections[i]->label : "";
      init_kf(t.kf, new_box);
      
      (*filter->active_tracks)[new_id] = std::move(t);
    }
  }

  // 5. Remove lost tracks
  for (auto it = filter->active_tracks->begin(); it != filter->active_tracks->end(); ) {
    if (it->second.missed_frames >= 30) {
      it = filter->active_tracks->erase(it);
    } else {
      ++it;
    }
  }

  return GST_FLOW_OK;
}

static gboolean
onnxtracker_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "onnxtracker", GST_RANK_NONE,
      GST_TYPE_ONNXTRACKER);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    onnxtracker,
    "Kalman Filter Tracker",
    onnxtracker_init,
    PACKAGE_VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
