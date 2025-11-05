#pragma once

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _GstMovpkgPersistentStreamInfo GstMovpkgPersistentStreamInfo;

typedef enum
{
  GST_MOVPKG_PERSISTENT_STREAM_INFO_TYPE_NONE,
  GST_MOVPKG_PERSISTENT_STREAM_INFO_TYPE_MAIN,
  GST_MOVPKG_PERSISTENT_STREAM_INFO_TYPE_SUPPLEMENTAL,
} GstMovpkgPersistentStreamInfoType;

typedef struct
{
  gchar *url;
  gchar *path;
  GstClockTime time;
  GstClockTime duration;
  gsize offset;
  gsize length;
  gssize sequence_number;
} GstMovpkgMediaSegment;

typedef struct
{
  gchar *url;
  gchar *path;
  gsize offset;
  gsize length;
  gssize sequence_number;
} GstMovpkgMediaInitSegment;

typedef struct
{
  gchar *type;
} GstMovpkgMediaType;

gboolean
gst_movpkg_parse_stream_info (GstMovpkgPersistentStreamInfo ** info,
                              const gchar                    * data,
                              gsize                            size,
                              GError                        ** error);

void
gst_movpkg_media_segment_free (GstMovpkgMediaSegment * self);

void
gst_movpkg_media_init_segment_free (GstMovpkgMediaInitSegment * self);

void
gst_movpkg_stream_info_free (GstMovpkgPersistentStreamInfo * self);

gboolean
gst_movpkg_stream_info_is_complete (GstMovpkgPersistentStreamInfo * self);

GstIterator *
gst_movpkg_stream_info_iter_segments (GstMovpkgPersistentStreamInfo * self,
                                      GMutex                        * mutex,
                                      guint32                       * cookie,
                                      GObject                       * parent);

GstIterator *
gst_movpkg_stream_info_iter_init_segments (GstMovpkgPersistentStreamInfo * self,
                                           GMutex                        * mutex,
                                           guint32                       * cookie,
                                           GObject                       * parent);

GstMovpkgMediaInitSegment *
gst_movpkg_stream_info_find_init_segment (GstMovpkgPersistentStreamInfo * self,
    gssize sequence_number);

GstIterator *
gst_movpkg_stream_info_iter_media_types (GstMovpkgPersistentStreamInfo * self,
                                         GMutex                        * mutex,
                                         guint32                       * cookie,
                                         GObject                       * parent);

G_END_DECLS
