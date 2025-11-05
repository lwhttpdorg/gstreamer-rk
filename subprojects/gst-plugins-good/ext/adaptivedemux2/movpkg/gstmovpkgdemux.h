#pragma once

#include <gst/gst.h>

#include "gstadaptivedemux.h"

G_BEGIN_DECLS

#define GST_TYPE_MOVPKG_DEMUX (gst_movpkg_demux_get_type())
G_DECLARE_FINAL_TYPE (GstMovpkgDemux, gst_movpkg_demux, GST, MOVPKG_DEMUX, GstAdaptiveDemux)
#define GST_MOVPKG_DEMUX_CAST(obj) ((GstMovpkgDemux *) (obj))

G_END_DECLS
