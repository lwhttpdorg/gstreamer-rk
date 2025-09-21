/* GStreamer ReplayGain volume adjustment
 *
 * Copyright (C) 2007 Rene Stadler <mail@renestadler.de>
 *
 * gstrgvolume.h: Element to apply ReplayGain volume adjustment
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#ifndef __GST_RG_VOLUME_H__
#define __GST_RG_VOLUME_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_RG_VOLUME (gst_rg_volume_get_type())
G_DECLARE_FINAL_TYPE (GstRgVolume, gst_rg_volume, GST, RG_VOLUME, GstBin)

/**
 * GstRgVolume:
 *
 * Opaque data structure.
 */
struct _GstRgVolume
{
  GstBin bin;

  /*< private >*/

  GstElement *volume_element;
  gdouble max_volume;

  gboolean album_mode;
  gdouble headroom;
  gdouble pre_amp;
  gdouble fallback_gain;

  gdouble target_gain;
  gdouble result_gain;

  gdouble track_gain;
  gdouble track_peak;
  gdouble album_gain;
  gdouble album_peak;

  gboolean has_track_gain;
  gboolean has_track_peak;
  gboolean has_album_gain;
  gboolean has_album_peak;

  gdouble reference_level;
};

GST_ELEMENT_REGISTER_DECLARE (rgvolume);

G_END_DECLS

#endif /* __GST_RG_VOLUME_H__ */
