/* GStreamer
 * Copyright (C) 2026 Dominique Leroux <dominique.p.leroux@gmail.com>
 *
 * gstzipfilesrc.c:
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
/**
 * SECTION:element-zipfilesrc
 * @title: zipfilesrc
 * @short_description: Read a stored member from a ZIP-compatible archive
 *
 * Read a stored member from a ZIP-compatible archive as a seekable byte stream.
 *
 * `zipfilesrc` intentionally only supports entries using compression method 0
 * (`STORE`). Compressed or encrypted entries are rejected because they cannot be
 * exposed as a direct byte range in the archive.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 zipfilesrc archive=media.zip location=video.webm ! matroskademux ! ...
 * ]|
 *
 * APK files are ZIP-compatible archives, so Android assets can also be read:
 * |[
 * gst-launch-1.0 zipfilesrc archive=app.apk location=assets/video.webm ! matroskademux ! ...
 * ]|
 *
 * Since: 1.30
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "gstzipfilesrc.h"
#include "gstzipparser.h"

#include <errno.h>
#include <fcntl.h>
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/types.h>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#ifdef G_OS_WIN32
#include <io.h>
#undef lseek
#define lseek _lseeki64
#undef off_t
#define off_t guint64
#endif

#ifndef O_BINARY
#define O_BINARY (0)
#endif

static const gchar gst_zip_file_src_jar_protocol[] = "jar";
static const gchar gst_zip_file_src_jar_scheme[] = "jar:";
static const gchar gst_zip_file_src_jar_separator[] = "!/";

GST_DEBUG_CATEGORY_STATIC (gst_zip_file_src_debug);
#define GST_CAT_DEFAULT gst_zip_file_src_debug

struct _GstZipFileSrc
{
  GstBaseSrc parent;

  gchar *archive;
  gchar *location;

  gint fd;
  guint64 payload_offset;
  guint64 payload_size;
  guint64 read_position;
};

enum
{
  PROP_0,
  PROP_ARCHIVE,
  PROP_LOCATION,
  PROP_URI,
};

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static void gst_zip_file_src_uri_handler_init (gpointer g_iface,
    gpointer iface_data);

#define gst_zip_file_src_parent_class parent_class
#define _do_init \
  G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER, gst_zip_file_src_uri_handler_init); \
  GST_DEBUG_CATEGORY_INIT (gst_zip_file_src_debug, "zipfilesrc", 0, \
      "ZIP file source");
G_DEFINE_TYPE_WITH_CODE (GstZipFileSrc, gst_zip_file_src, GST_TYPE_BASE_SRC,
    _do_init);
GST_ELEMENT_REGISTER_DEFINE (zip_file_src, "zipfilesrc", GST_RANK_NONE,
    GST_TYPE_ZIP_FILE_SRC);

static gboolean
gst_zip_file_src_check_mutable (GstZipFileSrc * src, GError ** error)
{
  GstState state;

  GST_OBJECT_LOCK (src);
  state = GST_STATE (src);
  if (state != GST_STATE_READY && state != GST_STATE_NULL) {
    GST_OBJECT_UNLOCK (src);
    g_set_error_literal (error, GST_URI_ERROR, GST_URI_ERROR_BAD_STATE,
        "Changing the archive member URI while open is not supported");
    return FALSE;
  }
  GST_OBJECT_UNLOCK (src);

  return TRUE;
}

static gboolean
gst_zip_file_src_open_member (GstZipFileSrc * src, GError ** error)
{
  gint64 archive_size = lseek (src->fd, 0, SEEK_END);
  GstZipEntryRange range;

  if (archive_size < 0) {
    g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
        "Could not determine ZIP archive size for '%s': %s", src->archive,
        g_strerror (errno));
    return FALSE;
  }

  if (!gst_zip_parser_find_stored_entry (src->fd, archive_size, src->location,
          &range, error))
    return FALSE;

  src->payload_offset = range.offset;
  src->payload_size = range.size;

  GST_INFO_OBJECT (src,
      "Resolved ZIP member %s in %s at offset %" G_GUINT64_FORMAT
      " size %" G_GUINT64_FORMAT, src->location, src->archive,
      src->payload_offset, src->payload_size);

  return TRUE;
}

static gchar *
gst_zip_file_src_build_uri (GstZipFileSrc * src)
{
  gchar *archive_uri;
  gchar *uri;
  GError *error = NULL;

  if (!src->archive || !src->location) {
    GST_LOG_OBJECT (src,
        "Cannot build URI without both archive and member location");
    return NULL;
  }

  archive_uri = gst_filename_to_uri (src->archive, &error);
  if (!archive_uri) {
    GST_WARNING_OBJECT (src, "Cannot build URI for archive '%s': %s",
        src->archive, error ? error->message : "unknown error");
    g_clear_error (&error);
    return NULL;
  }

  uri = g_strdup_printf ("%s%s%s%s", gst_zip_file_src_jar_scheme, archive_uri,
      gst_zip_file_src_jar_separator, src->location);
  g_free (archive_uri);

  return uri;
}

static gboolean
gst_zip_file_src_set_archive (GstZipFileSrc * src, const gchar * archive,
    GError ** error)
{
  if (!gst_zip_file_src_check_mutable (src, error))
    return FALSE;

  g_free (src->archive);
  src->archive = g_strdup (archive);
  g_object_notify (G_OBJECT (src), "archive");
  g_object_notify (G_OBJECT (src), "uri");

  return TRUE;
}

static gboolean
gst_zip_file_src_set_location (GstZipFileSrc * src, const gchar * location,
    GError ** error)
{
  if (!gst_zip_file_src_check_mutable (src, error))
    return FALSE;

  g_free (src->location);
  src->location = g_strdup (location);
  g_object_notify (G_OBJECT (src), "location");
  g_object_notify (G_OBJECT (src), "uri");

  return TRUE;
}

static gboolean
gst_zip_file_src_parse_jar_uri (GstZipFileSrc * src, const gchar * uri,
    GError ** error)
{
  const gchar *inner;
  const gchar *separator;
  gchar *file_uri = NULL;
  gchar *archive = NULL;
  gchar *member = NULL;
  gboolean ret = FALSE;

  if (!uri || !g_str_has_prefix (uri, gst_zip_file_src_jar_scheme)) {
    g_set_error (error, GST_URI_ERROR, GST_URI_ERROR_UNSUPPORTED_PROTOCOL,
        "Unsupported URI protocol for zipfilesrc");
    return FALSE;
  }

  inner = uri + strlen (gst_zip_file_src_jar_scheme);
  separator = strstr (inner, gst_zip_file_src_jar_separator);
  if (!separator) {
    g_set_error (error, GST_URI_ERROR, GST_URI_ERROR_BAD_URI,
        "Invalid jar URI '%s': missing '%s' separator", uri,
        gst_zip_file_src_jar_separator);
    return FALSE;
  }

  file_uri = g_strndup (inner, separator - inner);
  archive = g_filename_from_uri (file_uri, NULL, error);
  if (!archive)
    goto done;

  member = g_strdup (separator + strlen (gst_zip_file_src_jar_separator));
  if (member[0] == '\0') {
    g_set_error (error, GST_URI_ERROR, GST_URI_ERROR_BAD_URI,
        "Invalid jar URI '%s': missing member path", uri);
    goto done;
  }

  if (!gst_zip_file_src_check_mutable (src, error))
    goto done;

  g_object_freeze_notify (G_OBJECT (src));
  g_free (src->archive);
  src->archive = archive;
  archive = NULL;
  g_free (src->location);
  src->location = member;
  member = NULL;
  g_object_notify (G_OBJECT (src), "archive");
  g_object_notify (G_OBJECT (src), "location");
  g_object_notify (G_OBJECT (src), "uri");
  g_object_thaw_notify (G_OBJECT (src));

  ret = TRUE;

done:
  g_free (file_uri);
  g_free (archive);
  g_free (member);
  return ret;
}

static void
gst_zip_file_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstZipFileSrc *src = GST_ZIP_FILE_SRC (object);

  switch (prop_id) {
    case PROP_ARCHIVE:
      gst_zip_file_src_set_archive (src, g_value_get_string (value), NULL);
      break;
    case PROP_LOCATION:
      gst_zip_file_src_set_location (src, g_value_get_string (value), NULL);
      break;
    case PROP_URI:
      gst_zip_file_src_parse_jar_uri (src, g_value_get_string (value), NULL);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_zip_file_src_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstZipFileSrc *src = GST_ZIP_FILE_SRC (object);

  switch (prop_id) {
    case PROP_ARCHIVE:
      g_value_set_string (value, src->archive);
      break;
    case PROP_LOCATION:
      g_value_set_string (value, src->location);
      break;
    case PROP_URI:
      g_value_take_string (value, gst_zip_file_src_build_uri (src));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_zip_file_src_start (GstBaseSrc * basesrc)
{
  GstZipFileSrc *src = GST_ZIP_FILE_SRC (basesrc);
  GError *error = NULL;

  if (!src->archive || src->archive[0] == '\0')
    goto missing_archive;
  if (!src->location || src->location[0] == '\0')
    goto missing_location;

  src->fd = g_open (src->archive, O_RDONLY | O_BINARY, 0);
  if (src->fd < 0)
    goto open_failed;

  if (!gst_zip_file_src_open_member (src, &error))
    goto parse_failed;

  src->read_position = G_MAXUINT64;

  return TRUE;

missing_archive:
  GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND, (_("No archive specified")),
      ("Set the archive property or a supported URI"));
  return FALSE;
missing_location:
  GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND,
      (_("No member location specified")),
      ("Set the location property or a supported URI"));
  return FALSE;
open_failed:
  GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
      (_("Could not open ZIP archive \"%s\" for reading."), src->archive),
      ("%s", g_strerror (errno)));
  return FALSE;
parse_failed:
  GST_ELEMENT_ERROR (src, RESOURCE, READ,
      (_("Could not open ZIP member \"%s\"."), src->location),
      ("%s", error ? error->message : "unknown error"));
  if (error)
    g_error_free (error);
  close (src->fd);
  src->fd = -1;
  return FALSE;
}

static gboolean
gst_zip_file_src_stop (GstBaseSrc * basesrc)
{
  GstZipFileSrc *src = GST_ZIP_FILE_SRC (basesrc);

  if (src->fd >= 0) {
    close (src->fd);
    src->fd = -1;
  }
  src->payload_offset = 0;
  src->payload_size = 0;
  src->read_position = G_MAXUINT64;

  return TRUE;
}

static gboolean
gst_zip_file_src_is_seekable (GstBaseSrc * basesrc)
{
  return TRUE;
}

static gboolean
gst_zip_file_src_get_size (GstBaseSrc * basesrc, guint64 * size)
{
  GstZipFileSrc *src = GST_ZIP_FILE_SRC (basesrc);

  *size = src->payload_size;
  return TRUE;
}

static GstFlowReturn
gst_zip_file_src_fill (GstBaseSrc * basesrc, guint64 offset, guint length,
    GstBuffer * buf)
{
  GstZipFileSrc *src = GST_ZIP_FILE_SRC (basesrc);
  GstMapInfo info;
  guint bytes_to_read;
  guint remaining;
  guint bytes_read;
  guint64 archive_offset;
  guint8 *data;

  if (offset >= src->payload_size)
    goto eos;

  bytes_to_read = MIN ((guint64) length, src->payload_size - offset);
  if (!g_uint64_checked_add (&archive_offset, src->payload_offset, offset))
    goto offset_overflow;

  if (G_UNLIKELY (src->read_position != archive_offset)) {
    off_t res;

    /* lseek()-style APIs use signed 64-bit offsets. Reject values that cannot
     * be represented before they reach the platform call.
     */
    if (G_UNLIKELY (archive_offset > (guint64) G_MAXINT64))
      goto seek_failed;

    res = lseek (src->fd, archive_offset, SEEK_SET);
    if (G_UNLIKELY (res == (off_t) - 1 || res != archive_offset))
      goto seek_failed;

    src->read_position = archive_offset;
  }

  if (!gst_buffer_map (buf, &info, GST_MAP_WRITE))
    goto map_failed;
  if (G_UNLIKELY (info.size < bytes_to_read))
    goto buffer_too_small;
  data = info.data;

  bytes_read = 0;
  remaining = bytes_to_read;
  while (remaining > 0) {
    gint ret;

    GST_LOG_OBJECT (src, "Reading %u bytes at archive offset 0x%"
        G_GINT64_MODIFIER "x", remaining, archive_offset + bytes_read);
    errno = 0;
    ret = read (src->fd, data + bytes_read, remaining);
    if (ret < 0) {
      if (errno == EINTR || errno == EAGAIN)
        continue;
      goto read_failed;
    }

    if (ret == 0) {
      if (bytes_read > 0)
        break;
      goto eos_mapped;
    }

    remaining -= ret;
    bytes_read += ret;
    src->read_position += ret;
  }

  gst_buffer_unmap (buf, &info);
  if (bytes_read < bytes_to_read)
    gst_buffer_resize (buf, 0, bytes_read);

  /* Offsets describe the logical byte stream exposed by this source, i.e. the
   * stored member, not the containing archive.
   */
  GST_BUFFER_OFFSET (buf) = offset;
  GST_BUFFER_OFFSET_END (buf) = offset + bytes_read;

  return GST_FLOW_OK;

offset_overflow:
  GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
      ("ZIP member read offset overflow"));
  return GST_FLOW_ERROR;
seek_failed:
  GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL), GST_ERROR_SYSTEM);
  return GST_FLOW_ERROR;
eos:
  gst_buffer_resize (buf, 0, 0);
  return GST_FLOW_EOS;
eos_mapped:
  gst_buffer_unmap (buf, &info);
  gst_buffer_resize (buf, 0, 0);
  return GST_FLOW_EOS;
map_failed:
  GST_ELEMENT_ERROR (src, RESOURCE, WRITE, (NULL), ("Can't write to buffer"));
  return GST_FLOW_ERROR;
buffer_too_small:
  GST_ELEMENT_ERROR (src, RESOURCE, WRITE, (NULL),
      ("Mapped buffer size %" G_GSIZE_FORMAT
          " is smaller than requested read size %u", info.size, bytes_to_read));
  gst_buffer_unmap (buf, &info);
  return GST_FLOW_ERROR;
read_failed:
  GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL), ("%s", g_strerror (errno)));
  gst_buffer_unmap (buf, &info);
  gst_buffer_resize (buf, 0, 0);
  return GST_FLOW_ERROR;
}

static void
gst_zip_file_src_finalize (GObject * object)
{
  GstZipFileSrc *src = GST_ZIP_FILE_SRC (object);

  g_free (src->archive);
  g_free (src->location);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_zip_file_src_class_init (GstZipFileSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstBaseSrcClass *gstbasesrc_class = GST_BASE_SRC_CLASS (klass);

  gobject_class->set_property = gst_zip_file_src_set_property;
  gobject_class->get_property = gst_zip_file_src_get_property;
  gobject_class->finalize = gst_zip_file_src_finalize;

  g_object_class_install_property (gobject_class, PROP_ARCHIVE,
      g_param_spec_string ("archive", "Archive",
          "ZIP-compatible archive file to read from", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          G_PARAM_EXPLICIT_NOTIFY | GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "Member Location",
          "Path of the stored member inside the archive", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          G_PARAM_EXPLICIT_NOTIFY | GST_PARAM_MUTABLE_READY));
  g_object_class_install_property (gobject_class, PROP_URI,
      g_param_spec_string ("uri", "URI",
          "URI of the archive member, for example jar:file:///app.apk!/assets/video.webm",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          G_PARAM_EXPLICIT_NOTIFY | GST_PARAM_MUTABLE_READY));

  gst_element_class_set_static_metadata (gstelement_class,
      "ZIP File Source", "Source/File",
      "Read a stored member from a ZIP-compatible archive",
      "Dominique Leroux <dominique.p.leroux@gmail.com>");
  gst_element_class_add_static_pad_template (gstelement_class, &srctemplate);

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_zip_file_src_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_zip_file_src_stop);
  gstbasesrc_class->is_seekable =
      GST_DEBUG_FUNCPTR (gst_zip_file_src_is_seekable);
  gstbasesrc_class->get_size = GST_DEBUG_FUNCPTR (gst_zip_file_src_get_size);
  gstbasesrc_class->fill = GST_DEBUG_FUNCPTR (gst_zip_file_src_fill);
}

static void
gst_zip_file_src_init (GstZipFileSrc * src)
{
  src->fd = -1;
  src->read_position = G_MAXUINT64;
  gst_base_src_set_blocksize (GST_BASE_SRC (src), 4 * 1024);
}

static GstURIType
gst_zip_file_src_uri_get_type (GType type)
{
  return GST_URI_SRC;
}

static const gchar *const *
gst_zip_file_src_uri_get_protocols (GType type)
{
  static const gchar *protocols[] = { gst_zip_file_src_jar_protocol, NULL };

  return protocols;
}

static gchar *
gst_zip_file_src_uri_get_uri (GstURIHandler * handler)
{
  GstZipFileSrc *src = GST_ZIP_FILE_SRC (handler);

  return gst_zip_file_src_build_uri (src);
}

static gboolean
gst_zip_file_src_uri_set_uri (GstURIHandler * handler, const gchar * uri,
    GError ** error)
{
  GstZipFileSrc *src = GST_ZIP_FILE_SRC (handler);

  if (!uri || !g_str_has_prefix (uri, gst_zip_file_src_jar_scheme)) {
    g_set_error (error, GST_URI_ERROR, GST_URI_ERROR_UNSUPPORTED_PROTOCOL,
        "Unsupported URI protocol for zipfilesrc");
    return FALSE;
  }

  return gst_zip_file_src_parse_jar_uri (src, uri, error);
}

static void
gst_zip_file_src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = g_iface;

  iface->get_type = gst_zip_file_src_uri_get_type;
  iface->get_protocols = gst_zip_file_src_uri_get_protocols;
  iface->get_uri = gst_zip_file_src_uri_get_uri;
  iface->set_uri = gst_zip_file_src_uri_set_uri;
}
