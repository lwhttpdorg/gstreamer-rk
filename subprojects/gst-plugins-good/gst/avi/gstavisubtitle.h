
#ifndef __GSTAVISUBTITLE_H__
#define __GSTAVISUBTITLE_H__

#include <glib.h>
#include <glib-object.h>
#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_AVI_SUBTITLE (gst_avi_subtitle_get_type ())
G_DECLARE_FINAL_TYPE (GstAviSubtitle, gst_avi_subtitle, GST, AVI_SUBTITLE,
    GstElement)

struct _GstAviSubtitle
{
  GstElement parent;

  GstPad    *src;
  GstPad    *sink;

  GstBuffer *subfile;  /* the complete subtitle file in one buffer */
};

G_END_DECLS
#endif
