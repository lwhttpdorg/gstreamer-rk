#pragma once

#include <gst/gst.h>

G_BEGIN_DECLS

GstToc * gst_movpkg_parse_chapters_json (const gchar * data,
                                         gsize         size,
                                         GError     ** error);

G_END_DECLS
