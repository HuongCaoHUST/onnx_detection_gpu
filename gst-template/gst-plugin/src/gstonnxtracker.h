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

/* ByteTrack headers – bundled inside src/bytetrack/, resolved via meson include_directories */
#include "ByteTrack/BYTETracker.h"

G_BEGIN_DECLS

#define GST_TYPE_ONNXTRACKER (gst_onnxtracker_get_type())
#define GST_ONNXTRACKER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ONNXTRACKER,Gstonnxtracker))
#define GST_ONNXTRACKER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ONNXTRACKER,GstonnxtrackerClass))
#define GST_IS_ONNXTRACKER(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ONNXTRACKER))
#define GST_IS_ONNXTRACKER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ONNXTRACKER))

typedef struct _Gstonnxtracker Gstonnxtracker;
typedef struct _GstonnxtrackerClass GstonnxtrackerClass;

typedef enum {
  GST_ONNX_TRACKER_ALGO_IOU      = 0,  /* Simple IoU greedy matching          */
  GST_ONNX_TRACKER_ALGO_SORT     = 1,  /* SORT: Kalman Filter + IoU            */
  GST_ONNX_TRACKER_ALGO_BYTETRACK = 2  /* ByteTrack: dual-thresh + Hungarian   */
} GstOnnxTrackerAlgorithm;

#define GST_TYPE_ONNX_TRACKER_ALGORITHM (gst_onnxtracker_algorithm_get_type())
GType gst_onnxtracker_algorithm_get_type (void);

/* Track struct used by IOU and SORT algorithms */
struct Track {
  int track_id;
  int missed_frames;
  cv::KalmanFilter kf;
  cv::Rect predicted_box;
  cv::Rect last_box;
  std::string label;
};

struct _Gstonnxtracker {
  GstBaseTransform element;

  /* IOU / SORT state */
  gint next_track_id;
  std::map<int, Track> *active_tracks;

  /* Selected algorithm */
  GstOnnxTrackerAlgorithm tracker_algorithm;

  /* ByteTrack instance (created lazily when algorithm == BYTETRACK) */
  byte_track::BYTETracker *bytetracker;

  /* ByteTrack configuration properties */
  gint   bt_frame_rate;    /* fps assumed by the tracker (default: 30) */
  gint   bt_track_buffer;  /* frames to keep a lost track (default: 30) */
  gfloat bt_track_thresh;  /* min score to enter high-score set (default: 0.5)  */
  gfloat bt_high_thresh;   /* min score to init a new track    (default: 0.6)  */
  gfloat bt_match_thresh;  /* IoU threshold for assignment      (default: 0.8)  */
};

struct _GstonnxtrackerClass {
  GstBaseTransformClass parent_class;
};

GType gst_onnxtracker_get_type (void);

G_END_DECLS

#endif /* __GST_ONNX_TRACKER_H__ */
