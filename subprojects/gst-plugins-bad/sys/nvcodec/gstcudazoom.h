/* GStreamer
 * Copyright (C) 2025 David Maseda Neira <david.maseda@cinfo.es>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_CUDA_ZOOM_H__
#define __GST_CUDA_ZOOM_H__

#include <gst/gst.h>
#include "gstcudabasetransform.h"

G_BEGIN_DECLS

#define GST_TYPE_CUDA_ZOOM             (gst_cuda_zoom_get_type())
#define GST_CUDA_ZOOM(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CUDA_ZOOM,GstCudaZoom))
#define GST_CUDA_ZOOM_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CUDA_ZOOM,GstCudaZoomClass))
#define GST_IS_CUDA_ZOOM(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CUDA_ZOOM))
#define GST_IS_CUDA_ZOOM_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CUDA_ZOOM))

typedef struct _GstCudaZoom GstCudaZoom;
typedef struct _GstCudaZoomClass GstCudaZoomClass;

typedef struct _GstCudaConverter GstCudaConverter;

struct _GstCudaZoom
{
  GstCudaBaseTransform parent;

  GstCudaConverter *converter;

  gfloat zoom_factor;
  gint center_x;
  gint center_y;
  gint src_x;
  gint src_y;
  gint src_width;
  gint src_height;
  gboolean use_roi;

  GMutex lock;
};

struct _GstCudaZoomClass
{
  GstCudaBaseTransformClass parent_class;
};

GType gst_cuda_zoom_get_type (void);

G_END_DECLS

#endif /* __GST_CUDA_ZOOM_H__ */
