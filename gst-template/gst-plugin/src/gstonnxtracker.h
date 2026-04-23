/*
 * GStreamer
 * Copyright (C) 2026 HuongCao <user@hostname.org>
 */

#ifndef __GST_ONNX_TRACKER_H__
#define __GST_ONNX_TRACKER_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <opencv2/opencv.hpp>
#include <map>
#include <string>

G_BEGIN_DECLS

#define GST_TYPE_ONNXTRACKER (gst_onnxtracker_get_type())
#define GST_ONNXTRACKER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ONNXTRACKER,Gstonnxtracker))
#define GST_ONNXTRACKER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ONNXTRACKER,GstonnxtrackerClass))
#define GST_IS_ONNXTRACKER(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ONNXTRACKER))
#define GST_IS_ONNXTRACKER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ONNXTRACKER))

typedef struct _Gstonnxtracker Gstonnxtracker;
typedef struct _GstonnxtrackerClass GstonnxtrackerClass;

typedef enum {
  GST_ONNX_TRACKER_ALGO_IOU = 0,
  GST_ONNX_TRACKER_ALGO_SORT = 1
} GstOnnxTrackerAlgorithm;

#define GST_TYPE_ONNX_TRACKER_ALGORITHM (gst_onnxtracker_algorithm_get_type())
GType gst_onnxtracker_algorithm_get_type (void);

struct Track {
  int track_id;
  int missed_frames;
  cv::KalmanFilter kf;
  cv::Rect predicted_box;
  cv::Rect last_box; // Added to store the last known position for IoU tracking
  std::string label;
};

struct _Gstonnxtracker {
  GstBaseTransform element;

  gint next_track_id;
  std::map<int, Track> *active_tracks;
  GstOnnxTrackerAlgorithm tracker_algorithm;
};

struct _GstonnxtrackerClass {
  GstBaseTransformClass parent_class;
};

GType gst_onnxtracker_get_type (void);

G_END_DECLS

#endif /* __GST_ONNX_TRACKER_H__ */
