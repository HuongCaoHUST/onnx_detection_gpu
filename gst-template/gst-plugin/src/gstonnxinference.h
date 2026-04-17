/* 
 * GStreamer
 * Copyright (C) 2006 Stefan Kost <ensonic@users.sf.net>
 * Copyright (C) 2020 Niels De Graef <niels.degraef@gmail.com>
 * Copyright (C) 2026 HuongCao <<user@hostname.org>>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
 
#ifndef __GST_ONNXINFERENCE_H__
#define __GST_ONNXINFERENCE_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>

#ifdef __cplusplus
#include <onnxruntime_cxx_api.h>
#endif


G_BEGIN_DECLS

#define GST_TYPE_ONNXINFERENCE (gst_onnxinference_get_type())
G_DECLARE_FINAL_TYPE (Gstonnxinference, gst_onnxinference,
    GST, ONNXINFERENCE, GstBaseTransform)

struct _Gstonnxinference {
  GstBaseTransform element;

  gchar *model_location;

#ifdef __cplusplus
  Ort::Env *env;
  Ort::Session *session;
  Ort::MemoryInfo *memory_info;
  Ort::AllocatorWithDefaultOptions *allocator;
#else
  gpointer env;
  gpointer session;
  gpointer memory_info;
  gpointer allocator;
#endif
};

G_END_DECLS

#endif /* __GST_ONNXINFERENCE_H__ */
