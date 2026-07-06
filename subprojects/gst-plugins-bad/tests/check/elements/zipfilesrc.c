/* GStreamer
 * Copyright (C) 2026 Dominique Leroux <dominique.p.leroux@gmail.com>
 *
 * zipfilesrc.c:
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <fcntl.h>

#include <gst/check/gstharness.h>
#include <gst/check/gstcheck.h>
#include <glib/gstdio.h>

#include "../../../gst/archive/gstzipparser.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef G_OS_WIN32
#include <io.h>
#undef close
#define close _close
#undef lseek
#define lseek _lseeki64
#endif

#define ZIP_TEST_MEMBER_NAME "assets/movie.webm"
#define ZIP_TEST_MISSING_MEMBER_NAME "assets/missing.webm"
#define ZIP_COMPRESSION_METHOD_DEFLATE 8

/* Append @value to @data as a 16-bit little-endian integer. */
static void
append_le16 (GByteArray * data, guint16 value)
{
  guint8 bytes[2];

  GST_WRITE_UINT16_LE (bytes, value);
  g_byte_array_append (data, bytes, sizeof (bytes));
}

/* Append @value to @data as a 32-bit little-endian integer. */
static void
append_le32 (GByteArray * data, guint32 value)
{
  guint8 bytes[4];

  GST_WRITE_UINT32_LE (bytes, value);
  g_byte_array_append (data, bytes, sizeof (bytes));
}

/* Append @len bytes from @bytes to @data. */
static void
append_bytes (GByteArray * data, const guint8 * bytes, guint len)
{
  g_byte_array_append (data, bytes, len);
}

/* Append a local header for @name using @method and @payload. */
static guint32
append_local_file_header (GByteArray * data, const gchar * name,
    guint16 method, const guint8 * payload, guint32 payload_size)
{
  guint32 offset = data->len;
  guint16 name_len = strlen (name);

  /* PKWARE APPNOTE 4.3.7 local file header. */
  /* local file header signature, 4 bytes */
  append_le32 (data, GST_ZIP_LOCAL_FILE_HEADER_SIGNATURE);
  /* version needed to extract, 2 bytes */
  append_le16 (data, 20);
  /* general purpose bit flag, 2 bytes */
  append_le16 (data, 0);
  /* compression method, 2 bytes */
  append_le16 (data, method);
  /* last mod file time, 2 bytes */
  append_le16 (data, 0);
  /* last mod file date, 2 bytes */
  append_le16 (data, 0);
  /* crc-32, 4 bytes */
  append_le32 (data, 0);
  /* compressed size, 4 bytes */
  append_le32 (data, payload_size);
  /* uncompressed size, 4 bytes */
  append_le32 (data, payload_size);
  /* file name length, 2 bytes */
  append_le16 (data, name_len);
  /* extra field length, 2 bytes */
  append_le16 (data, 0);
  /* file name, variable size */
  append_bytes (data, (const guint8 *) name, name_len);
  /* file data, variable size */
  append_bytes (data, payload, payload_size);

  return offset;
}

/* Append central directory metadata for @name and its local header offset. */
static void
append_central_file_header (GByteArray * data, const gchar * name,
    guint16 method, guint32 compressed_size, guint32 uncompressed_size,
    guint32 local_header_offset, const guint8 * extra, guint16 extra_len)
{
  guint16 name_len = strlen (name);

  /* PKWARE APPNOTE 4.3.12 central directory file header. */
  /* central file header signature, 4 bytes */
  append_le32 (data, GST_ZIP_CENTRAL_DIRECTORY_FILE_HEADER_SIGNATURE);
  /* version made by, 2 bytes */
  append_le16 (data, 20);
  /* version needed to extract, 2 bytes */
  append_le16 (data, 20);
  /* general purpose bit flag, 2 bytes */
  append_le16 (data, 0);
  /* compression method, 2 bytes */
  append_le16 (data, method);
  /* last mod file time, 2 bytes */
  append_le16 (data, 0);
  /* last mod file date, 2 bytes */
  append_le16 (data, 0);
  /* crc-32, 4 bytes */
  append_le32 (data, 0);
  /* compressed size, 4 bytes */
  append_le32 (data, compressed_size);
  /* uncompressed size, 4 bytes */
  append_le32 (data, uncompressed_size);
  /* file name length, 2 bytes */
  append_le16 (data, name_len);
  /* extra field length, 2 bytes */
  append_le16 (data, extra_len);
  /* file comment length, 2 bytes */
  append_le16 (data, 0);
  /* disk number start, 2 bytes */
  append_le16 (data, 0);
  /* internal file attributes, 2 bytes */
  append_le16 (data, 0);
  /* external file attributes, 4 bytes */
  append_le32 (data, 0);
  /* relative offset of local header, 4 bytes */
  append_le32 (data, local_header_offset);
  /* file name, variable size */
  append_bytes (data, (const guint8 *) name, name_len);
  if (extra_len > 0)
    /* extra field, variable size */
    append_bytes (data, extra, extra_len);
}

/* Append an EOCD record describing the central directory byte range. */
static void
append_eocd_with_comment (GByteArray * data, guint16 entries,
    guint32 central_dir_size, guint32 central_dir_offset,
    const guint8 * comment, guint16 comment_len)
{
  /* PKWARE APPNOTE 4.3.16 end of central directory record. */
  /* end of central directory signature, 4 bytes */
  append_le32 (data, GST_ZIP_EOCD_SIGNATURE);
  /* number of this disk, 2 bytes */
  append_le16 (data, 0);
  /* number of the disk with the start of the central directory, 2 bytes */
  append_le16 (data, 0);
  /* total number of entries in the central directory on this disk, 2 bytes */
  append_le16 (data, entries);
  /* total number of entries in the central directory, 2 bytes */
  append_le16 (data, entries);
  /* size of the central directory, 4 bytes */
  append_le32 (data, central_dir_size);
  /* offset of start of central directory, 4 bytes */
  append_le32 (data, central_dir_offset);
  /* .ZIP file comment length, 2 bytes */
  append_le16 (data, comment_len);
  if (comment_len > 0)
    /* .ZIP file comment, variable size */
    append_bytes (data, comment, comment_len);
}

static void
append_eocd (GByteArray * data, guint16 entries, guint32 central_dir_size,
    guint32 central_dir_offset)
{
  append_eocd_with_comment (data, entries, central_dir_size, central_dir_offset,
      NULL, 0);
}

/* Write @data to a temporary archive and return its filename. */
static gchar *
write_archive (GByteArray * data)
{
  GError *error = NULL;
  gchar *path = NULL;
  gint fd = g_file_open_tmp ("zipfilesrc-XXXXXX.zip", &path, &error);

  fail_unless (error == NULL, "Unexpected error: %s",
      error ? error->message : "none");
  fail_unless (fd >= 0);
  close (fd);

  fail_unless (g_file_set_contents (path, (const gchar *) data->data,
          data->len, &error));
  fail_unless (error == NULL, "Unexpected error: %s",
      error ? error->message : "none");

  return path;
}

/* Resolve @location from the temporary archive at @path. */
static gboolean
parse_archive (const gchar * path, const gchar * location,
    GstZipEntryRange * range, GError ** error)
{
  gint fd = g_open (path, O_RDONLY, 0);
  gint64 size;
  gboolean ret;

  fail_unless (fd >= 0);

  size = lseek (fd, 0, SEEK_END);
  fail_unless (size >= 0);

  ret = gst_zip_parser_find_stored_entry (fd, size, location, range, error);
  close (fd);

  return ret;
}

/* Assert that @range points to @payload inside the archive at @path. */
static void
assert_archive_range_matches_payload (const gchar * path,
    const GstZipEntryRange * range, const guint8 * payload, guint payload_size)
{
  GError *error = NULL;
  gchar *contents = NULL;
  gsize length;

  fail_unless (g_file_get_contents (path, &contents, &length, &error));
  fail_unless (error == NULL, "Unexpected error: %s",
      error ? error->message : "none");

  fail_unless_equals_uint64 (range->size, payload_size);
  fail_unless (range->offset <= (guint64) length);
  fail_unless (range->size <= (guint64) length - range->offset);
  fail_unless_equals_int (memcmp (contents + range->offset, payload,
          payload_size), 0);

  g_free (contents);
}

/* Create an archive containing one member with @name, @method, and @payload. */
static gchar *
create_single_member_archive (const gchar * name, guint16 method,
    const guint8 * payload, guint32 payload_size, gboolean zip64_member)
{
  GByteArray *data = g_byte_array_new ();
  guint32 local_offset = append_local_file_header (data, name, method, payload,
      payload_size);
  guint32 central_offset = data->len;
  guint32 central_size;
  gchar *path;

  if (zip64_member) {
    guint8 extra[28];

    /* PKWARE APPNOTE 4.5.3 Zip64 extended information extra field. */
    /* header ID, 2 bytes */
    GST_WRITE_UINT16_LE (extra, GST_ZIP64_EXTENDED_INFORMATION_EXTRA_FIELD_ID);
    /* data size, 2 bytes */
    GST_WRITE_UINT16_LE (extra + 2, 24);
    /* original size, 8 bytes */
    GST_WRITE_UINT64_LE (extra + 4, payload_size);
    /* compressed size, 8 bytes */
    GST_WRITE_UINT64_LE (extra + 12, payload_size);
    /* relative header offset, 8 bytes */
    GST_WRITE_UINT64_LE (extra + 20, local_offset);
    append_central_file_header (data, name, method, GST_ZIP_UINT32_SENTINEL,
        GST_ZIP_UINT32_SENTINEL,
        GST_ZIP_UINT32_SENTINEL, extra, sizeof (extra));
  } else {
    append_central_file_header (data, name, method, payload_size, payload_size,
        local_offset, NULL, 0);
  }

  central_size = data->len - central_offset;
  append_eocd (data, 1, central_size, central_offset);

  path = write_archive (data);
  g_byte_array_unref (data);

  return path;
}

/* Create an archive with a trailing ZIP comment after the EOCD record. */
static gchar *
create_single_member_archive_with_comment (const gchar * name,
    const guint8 * payload, guint32 payload_size)
{
  const guint8 comment[] = "comment";
  GByteArray *data = g_byte_array_new ();
  guint32 local_offset = append_local_file_header (data, name,
      GST_ZIP_COMPRESSION_METHOD_STORE, payload, payload_size);
  guint32 central_offset = data->len;
  guint32 central_size;
  gchar *path;

  append_central_file_header (data, name, GST_ZIP_COMPRESSION_METHOD_STORE,
      payload_size, payload_size, local_offset, NULL, 0);
  central_size = data->len - central_offset;
  append_eocd_with_comment (data, 1, central_size, central_offset, comment,
      sizeof (comment) - 1);

  path = write_archive (data);
  g_byte_array_unref (data);

  return path;
}

/* Build a jar: URI for @path and @location. */
static gchar *
create_jar_uri (const gchar * path, const gchar * location)
{
  GError *error = NULL;
  gchar *file_uri = g_filename_to_uri (path, NULL, &error);
  gchar *jar_uri;

  fail_unless (file_uri != NULL);
  fail_unless (error == NULL, "Unexpected error: %s",
      error ? error->message : "none");

  jar_uri = g_strdup_printf ("jar:%s!/%s", file_uri, location);
  g_free (file_uri);

  return jar_uri;
}

static void
count_notify (GObject * object, GParamSpec * pspec, gpointer user_data)
{
  guint *count = user_data;

  (*count)++;
}

/* Assert that @buf contains exactly @payload at member-relative offsets. */
static void
assert_buffer_matches_payload (GstBuffer * buf, const guint8 * payload,
    guint payload_size)
{
  fail_unless (buf != NULL);
  fail_unless_equals_uint64 (GST_BUFFER_OFFSET (buf), 0);
  fail_unless_equals_uint64 (GST_BUFFER_OFFSET_END (buf), payload_size);
  gst_check_buffer_data (buf, payload, payload_size);
}

/* Assert that the virtual uri property matches @path and @location. */
static void
assert_zipfilesrc_uri_property_matches (GstElement * src, const gchar * path,
    const gchar * location)
{
  gchar *expected_uri = create_jar_uri (path, location);
  gchar *actual_uri = NULL;

  g_object_get (src, "uri", &actual_uri, NULL);
  fail_unless (gst_uri_is_valid (actual_uri));
  fail_unless_equals_string (actual_uri, expected_uri);

  g_free (actual_uri);
  g_free (expected_uri);
}

/* Read @path through zipfilesrc using archive/location properties. */
static void
assert_zipfilesrc_archive_location_reads_payload (const gchar * path,
    const guint8 * payload, guint payload_size)
{
  GstElement *src = gst_element_factory_make ("zipfilesrc", NULL);
  GstHarness *h;
  GstBuffer *buf = NULL;

  fail_unless (src != NULL);
  g_object_set (src, "archive", path, "location", ZIP_TEST_MEMBER_NAME, NULL);
  assert_zipfilesrc_uri_property_matches (src, path, ZIP_TEST_MEMBER_NAME);

  h = gst_harness_new_with_element (src, NULL, "src");
  gst_harness_play (h);

  fail_unless (gst_harness_pull_until_eos (h, &buf));
  assert_buffer_matches_payload (buf, payload, payload_size);
  gst_buffer_unref (buf);

  gst_harness_teardown (h);
  gst_object_unref (src);
}

/* Assert that zipfilesrc reports member-relative size and seekability. */
static void
assert_zipfilesrc_archive_location_reports_size_and_seekable (const gchar *
    path, guint payload_size)
{
  GstElement *src = gst_element_factory_make ("zipfilesrc", NULL);
  GstHarness *h;
  GstQuery *query;
  gint64 duration;
  gboolean seekable;
  gint64 start;
  gint64 end;

  fail_unless (src != NULL);
  g_object_set (src, "archive", path, "location", ZIP_TEST_MEMBER_NAME, NULL);

  h = gst_harness_new_with_element (src, NULL, "src");
  gst_harness_play (h);

  fail_unless (gst_element_query_duration (src, GST_FORMAT_BYTES, &duration));
  fail_unless_equals_int64 (duration, payload_size);

  query = gst_query_new_seeking (GST_FORMAT_BYTES);
  fail_unless (gst_element_query (src, query));
  gst_query_parse_seeking (query, NULL, &seekable, &start, &end);
  fail_unless (seekable);
  fail_unless_equals_int64 (start, 0);
  fail_unless_equals_int64 (end, payload_size);
  gst_query_unref (query);

  gst_harness_teardown (h);
  gst_object_unref (src);
}

/* Read @path through zipfilesrc using the jar: URI property. */
static void
assert_zipfilesrc_uri_reads_payload (const gchar * path, const guint8 * payload,
    guint payload_size)
{
  GstElement *src = gst_element_factory_make ("zipfilesrc", NULL);
  gchar *uri = create_jar_uri (path, ZIP_TEST_MEMBER_NAME);
  gchar *archive = NULL;
  gchar *location = NULL;
  GstHarness *h;
  GstBuffer *buf = NULL;
  guint uri_notify_count = 0;

  fail_unless (src != NULL);
  g_signal_connect (src, "notify::uri", G_CALLBACK (count_notify),
      &uri_notify_count);
  g_object_set (src, "uri", uri, NULL);
  fail_unless_equals_int (uri_notify_count, 1);
  g_object_get (src, "archive", &archive, "location", &location, NULL);
  fail_unless_equals_string (archive, path);
  fail_unless_equals_string (location, ZIP_TEST_MEMBER_NAME);
  assert_zipfilesrc_uri_property_matches (src, path, ZIP_TEST_MEMBER_NAME);

  h = gst_harness_new_with_element (src, NULL, "src");
  gst_harness_play (h);

  fail_unless (gst_harness_pull_until_eos (h, &buf));
  assert_buffer_matches_payload (buf, payload, payload_size);
  gst_buffer_unref (buf);

  gst_harness_teardown (h);
  gst_object_unref (src);
  g_free (archive);
  g_free (location);
  g_free (uri);
}

GST_START_TEST (test_stored_member)
{
  const guint8 payload[] = { 0x00, 0x01, 0x02, 0x03 };
  gchar *path = create_single_member_archive (ZIP_TEST_MEMBER_NAME,
      GST_ZIP_COMPRESSION_METHOD_STORE, payload, sizeof (payload), FALSE);
  GstZipEntryRange range;
  GError *error = NULL;

  fail_unless (parse_archive (path, ZIP_TEST_MEMBER_NAME, &range, &error));
  fail_unless (error == NULL, "Unexpected error: %s",
      error ? error->message : "none");
  assert_archive_range_matches_payload (path, &range, payload,
      sizeof (payload));

  g_remove (path);
  g_free (path);
}

GST_END_TEST;

GST_START_TEST (test_stored_member_with_archive_comment)
{
  const guint8 payload[] = { 0x04, 0x05, 0x06, 0x07 };
  gchar *path = create_single_member_archive_with_comment (ZIP_TEST_MEMBER_NAME,
      payload, sizeof (payload));
  GstZipEntryRange range;
  GError *error = NULL;

  fail_unless (parse_archive (path, ZIP_TEST_MEMBER_NAME, &range, &error));
  fail_unless (error == NULL, "Unexpected error: %s",
      error ? error->message : "none");
  assert_archive_range_matches_payload (path, &range, payload,
      sizeof (payload));

  g_remove (path);
  g_free (path);
}

GST_END_TEST;

GST_START_TEST (test_compressed_member_rejected)
{
  const guint8 payload[] = { 0x03, 0x00 };
  gchar *path = create_single_member_archive (ZIP_TEST_MEMBER_NAME,
      ZIP_COMPRESSION_METHOD_DEFLATE, payload, sizeof (payload), FALSE);
  GstZipEntryRange range;
  GError *error = NULL;

  fail_if (parse_archive (path, ZIP_TEST_MEMBER_NAME, &range, &error));
  fail_unless (g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_INVAL));
  g_clear_error (&error);

  g_remove (path);
  g_free (path);
}

GST_END_TEST;

GST_START_TEST (test_missing_member_rejected)
{
  const guint8 payload[] = { 0x00 };
  gchar *path = create_single_member_archive (ZIP_TEST_MEMBER_NAME,
      GST_ZIP_COMPRESSION_METHOD_STORE, payload, sizeof (payload), FALSE);
  GstZipEntryRange range;
  GError *error = NULL;

  fail_if (parse_archive (path, ZIP_TEST_MISSING_MEMBER_NAME, &range, &error));
  fail_unless (g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT));
  g_clear_error (&error);

  g_remove (path);
  g_free (path);
}

GST_END_TEST;

GST_START_TEST (test_malformed_central_directory_bounds_rejected)
{
  GByteArray *data = g_byte_array_new ();
  gchar *path = NULL;
  GstZipEntryRange range;
  GError *error = NULL;

  /* Write only an EOCD that claims one byte of central directory at offset 0.
   * That range points at the EOCD signature, so it is too short to contain a
   * central directory file header and must be rejected before entry parsing.
   */
  append_eocd (data, 1, 1, 0);
  path = write_archive (data);
  g_byte_array_unref (data);

  fail_if (parse_archive (path, ZIP_TEST_MEMBER_NAME, &range, &error));
  fail_unless (g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_INVAL));
  g_clear_error (&error);

  g_remove (path);
  g_free (path);
}

GST_END_TEST;

GST_START_TEST (test_local_header_range_rejected)
{
  GByteArray *data = g_byte_array_new ();
  gchar *path = NULL;
  GstZipEntryRange range;
  GError *error = NULL;
  guint32 central_size;

  /* Write a central directory entry whose local header offset points beyond
   * the archive. This exercises the parser's bounds check before it reads the
   * local header to compute the payload range.
   */
  append_central_file_header (data, ZIP_TEST_MEMBER_NAME,
      GST_ZIP_COMPRESSION_METHOD_STORE, 4, 4, 0x1000, NULL, 0);
  central_size = data->len;
  append_eocd (data, 1, central_size, 0);
  path = write_archive (data);
  g_byte_array_unref (data);

  fail_if (parse_archive (path, ZIP_TEST_MEMBER_NAME, &range, &error));
  fail_unless (g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_INVAL));
  g_clear_error (&error);

  g_remove (path);
  g_free (path);
}

GST_END_TEST;

GST_START_TEST (test_zip64_member_metadata)
{
  const guint8 payload[] = { 0x10, 0x11, 0x12 };
  gchar *path = create_single_member_archive (ZIP_TEST_MEMBER_NAME,
      GST_ZIP_COMPRESSION_METHOD_STORE, payload, sizeof (payload), TRUE);
  GstZipEntryRange range;
  GError *error = NULL;

  fail_unless (parse_archive (path, ZIP_TEST_MEMBER_NAME, &range, &error));
  fail_unless (error == NULL, "Unexpected error: %s",
      error ? error->message : "none");
  assert_archive_range_matches_payload (path, &range, payload,
      sizeof (payload));

  g_remove (path);
  g_free (path);
}

GST_END_TEST;

GST_START_TEST (test_element_archive_location_reads_member)
{
  const guint8 payload[] = { 0x20, 0x21, 0x22, 0x23, 0x24 };
  gchar *path = create_single_member_archive (ZIP_TEST_MEMBER_NAME,
      GST_ZIP_COMPRESSION_METHOD_STORE, payload, sizeof (payload), FALSE);

  assert_zipfilesrc_archive_location_reads_payload (path, payload,
      sizeof (payload));

  g_remove (path);
  g_free (path);
}

GST_END_TEST;

GST_START_TEST (test_element_archive_location_reports_size_and_seekable)
{
  const guint8 payload[] = { 0x25, 0x26, 0x27, 0x28, 0x29 };
  gchar *path = create_single_member_archive (ZIP_TEST_MEMBER_NAME,
      GST_ZIP_COMPRESSION_METHOD_STORE, payload, sizeof (payload), FALSE);

  assert_zipfilesrc_archive_location_reports_size_and_seekable (path,
      sizeof (payload));

  g_remove (path);
  g_free (path);
}

GST_END_TEST;

GST_START_TEST (test_element_uri_reads_member)
{
  const guint8 payload[] = { 0x30, 0x31, 0x32, 0x33 };
  gchar *path = create_single_member_archive (ZIP_TEST_MEMBER_NAME,
      GST_ZIP_COMPRESSION_METHOD_STORE, payload, sizeof (payload), FALSE);

  assert_zipfilesrc_uri_reads_payload (path, payload, sizeof (payload));

  g_remove (path);
  g_free (path);
}

GST_END_TEST;

GST_START_TEST (test_element_compressed_member_rejected)
{
  const guint8 payload[] = { 0x03, 0x00 };
  gchar *path = create_single_member_archive (ZIP_TEST_MEMBER_NAME,
      ZIP_COMPRESSION_METHOD_DEFLATE, payload, sizeof (payload), FALSE);
  GstElement *src = gst_element_factory_make ("zipfilesrc", NULL);

  fail_unless (src != NULL);
  g_object_set (src, "archive", path, "location", ZIP_TEST_MEMBER_NAME, NULL);

  fail_unless_equals_int (gst_element_set_state (src, GST_STATE_PAUSED),
      GST_STATE_CHANGE_FAILURE);
  gst_element_set_state (src, GST_STATE_NULL);
  gst_object_unref (src);

  g_remove (path);
  g_free (path);
}

GST_END_TEST;

static Suite *
zipfilesrc_suite (void)
{
  Suite *s = suite_create ("zipfilesrc");
  TCase *tc_parser = tcase_create ("parser");
  TCase *tc_element = tcase_create ("element");

  tcase_add_test (tc_parser, test_stored_member);
  tcase_add_test (tc_parser, test_stored_member_with_archive_comment);
  tcase_add_test (tc_parser, test_compressed_member_rejected);
  tcase_add_test (tc_parser, test_missing_member_rejected);
  tcase_add_test (tc_parser, test_malformed_central_directory_bounds_rejected);
  tcase_add_test (tc_parser, test_local_header_range_rejected);
  tcase_add_test (tc_parser, test_zip64_member_metadata);
  suite_add_tcase (s, tc_parser);

  tcase_add_test (tc_element, test_element_archive_location_reads_member);
  tcase_add_test (tc_element,
      test_element_archive_location_reports_size_and_seekable);
  tcase_add_test (tc_element, test_element_uri_reads_member);
  tcase_add_test (tc_element, test_element_compressed_member_rejected);
  suite_add_tcase (s, tc_element);

  return s;
}

GST_CHECK_MAIN (zipfilesrc);
