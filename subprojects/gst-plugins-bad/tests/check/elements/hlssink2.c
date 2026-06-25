/* GStreamer unit test for hlssink2
 *
 * Copyright (C) 2026 Wojciech Kapsa <kapsa.wojtek@gmail.com>
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
#  include "config.h"
#endif

#include <glib/gstdio.h>

#include <gst/check/gstcheck.h>
#include <gst/app/app.h>

#define FPS 30
#define GOP_SIZE 30
#define NUM_BUFFERS 90
#define EXPECTED_SEGMENTS (NUM_BUFFERS / GOP_SIZE)

/* For the rotation test: more segments than max-files, so the oldest get
 * deleted and an hls-segment-removed message is posted for each */
#define MAX_FILES_ROTATE 2
#define NUM_BUFFERS_ROTATE 150
#define EXPECTED_SEGMENTS_ROTATE (NUM_BUFFERS_ROTATE / GOP_SIZE)

static gchar *tmpdir = NULL;

static void
tempdir_setup (void)
{
  tmpdir = g_dir_make_tmp ("hlssink2-test-XXXXXX", NULL);
  fail_if (tmpdir == NULL);
}

static void
tempdir_cleanup (void)
{
  GDir *d;
  const gchar *f;

  fail_if (tmpdir == NULL);

  d = g_dir_open (tmpdir, 0, NULL);
  fail_if (d == NULL);

  while ((f = g_dir_read_name (d)) != NULL) {
    gchar *fname = g_build_filename (tmpdir, f, NULL);
    fail_if (g_remove (fname) != 0, "Failed to remove tmp file %s", fname);
    g_free (fname);
  }
  g_dir_close (d);

  fail_if (g_remove (tmpdir) != 0, "Failed to delete tmpdir %s", tmpdir);

  g_free (tmpdir);
  tmpdir = NULL;
}

static guint
count_files (const gchar * target)
{
  GDir *d;
  const gchar *f;
  guint ret = 0;

  d = g_dir_open (target, 0, NULL);
  fail_if (d == NULL);

  while ((f = g_dir_read_name (d)) != NULL)
    ret++;
  g_dir_close (d);

  return ret;
}

static void
dump_error (GstMessage * msg)
{
  GError *err = NULL;
  gchar *dbg_info;

  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR);

  gst_message_parse_error (msg, &err, &dbg_info);

  g_printerr ("ERROR from element %s: %s\n",
      GST_OBJECT_NAME (msg->src), err->message);
  g_printerr ("Debugging info: %s\n", (dbg_info) ? dbg_info : "none");
  g_error_free (err);
  g_free (dbg_info);
}

/* Creates buffers that look like H.264 byte-stream access units. mpegtsmux
 * does not parse the bitstream, so only the caps and the delta-unit flag
 * matter for fragmenting, the payload can be arbitrary. */
static GstBuffer *
create_buffer (guint index)
{
  static const guint8 keyframe_data[] = {
    0x00, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x00, 0x10, 0xff, 0xff, 0xf8
  };
  static const guint8 delta_data[] = {
    0x00, 0x00, 0x00, 0x01, 0x41, 0x9a, 0x24, 0x6c, 0x41, 0x4f, 0xff, 0xff
  };
  GstBuffer *buf;

  if (index % GOP_SIZE == 0) {
    buf = gst_buffer_new_memdup (keyframe_data, sizeof (keyframe_data));
  } else {
    buf = gst_buffer_new_memdup (delta_data, sizeof (delta_data));
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT);
  }

  GST_BUFFER_PTS (buf) = gst_util_uint64_scale (index, GST_SECOND, FPS);
  GST_BUFFER_DTS (buf) = GST_BUFFER_PTS (buf);
  /* Make sure the buffers align without rounding gaps */
  GST_BUFFER_DURATION (buf) =
      gst_util_uint64_scale (index + 1, GST_SECOND, FPS) - GST_BUFFER_PTS (buf);

  return buf;
}

GST_START_TEST (test_hlssink2_element_messages)
{
  GstElement *pipeline, *src, *sink;
  GstBus *bus;
  GstMessage *msg;
  GstCaps *caps;
  GPtrArray *segment_locations;
  gchar *launch, *playlist_location, *playlist_content, *expected_location;
  guint num_segments = 0, num_playlists = 0;
  guint i;

  playlist_location = g_build_filename (tmpdir, "playlist.m3u8", NULL);

  launch = g_strdup_printf ("hlssink2 name=sink target-duration=1 "
      "send-keyframe-requests=false playlist-length=0 "
      "location=%s/segment%%05d.ts playlist-location=%s "
      "appsrc name=src format=time ! sink.video", tmpdir, playlist_location);
  pipeline = gst_parse_launch (launch, NULL);
  fail_if (pipeline == NULL);
  g_free (launch);

  src = gst_bin_get_by_name (GST_BIN (pipeline), "src");
  fail_if (src == NULL);
  sink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");
  fail_if (sink == NULL);

  caps = gst_caps_new_simple ("video/x-h264",
      "stream-format", G_TYPE_STRING, "byte-stream",
      "alignment", G_TYPE_STRING, "au",
      "width", G_TYPE_INT, 320, "height", G_TYPE_INT, 240,
      "framerate", GST_TYPE_FRACTION, FPS, 1, NULL);
  g_object_set (src, "caps", caps, NULL);
  gst_caps_unref (caps);

  fail_if (gst_element_set_state (pipeline, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE);

  for (i = 0; i < NUM_BUFFERS; i++) {
    fail_unless (gst_app_src_push_buffer (GST_APP_SRC (src),
            create_buffer (i)) == GST_FLOW_OK);
  }
  fail_unless (gst_app_src_end_of_stream (GST_APP_SRC (src)) == GST_FLOW_OK);

  segment_locations = g_ptr_array_new_with_free_func (g_free);

  bus = gst_element_get_bus (pipeline);
  do {
    const GstStructure *s;

    msg = gst_bus_poll (bus,
        GST_MESSAGE_EOS | GST_MESSAGE_ERROR | GST_MESSAGE_ELEMENT, -1);
    fail_unless (msg != NULL);

    if (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_ELEMENT)
      break;

    s = gst_message_get_structure (msg);
    if (gst_structure_has_name (s, "hls-segment-added")) {
      const gchar *location;
      GstClockTime running_time, duration;

      fail_unless (GST_MESSAGE_SRC (msg) == GST_OBJECT_CAST (sink));

      location = gst_structure_get_string (s, "location");
      fail_unless (location != NULL);
      fail_unless (g_file_test (location, G_FILE_TEST_IS_REGULAR),
          "Segment '%s' does not exist when the message is posted", location);

      fail_unless (gst_structure_get_clock_time (s, "running-time",
              &running_time));
      assert_equals_uint64 (running_time, num_segments * GST_SECOND);

      /* The closing running time of the very last fragment corresponds to
       * the start of its last buffer, so the last segment may be reported
       * up to one frame short */
      fail_unless (gst_structure_get_clock_time (s, "duration", &duration));
      if (num_segments < EXPECTED_SEGMENTS - 1) {
        assert_equals_uint64 (duration, GST_SECOND);
      } else {
        fail_unless (duration <= GST_SECOND &&
            duration >= GST_SECOND - gst_util_uint64_scale_ceil (1, GST_SECOND,
                FPS), "Unexpected duration %" GST_TIME_FORMAT
            " of the last segment", GST_TIME_ARGS (duration));
      }

      g_ptr_array_add (segment_locations, g_strdup (location));
      num_segments++;
    } else if (gst_structure_has_name (s, "hls-playlist-written")) {
      const gchar *location;

      fail_unless (GST_MESSAGE_SRC (msg) == GST_OBJECT_CAST (sink));

      location = gst_structure_get_string (s, "location");
      fail_unless (location != NULL);
      assert_equals_string (location, playlist_location);
      fail_unless (g_file_test (location, G_FILE_TEST_IS_REGULAR),
          "Playlist '%s' does not exist when the message is posted", location);

      num_playlists++;
      /* Each segment is announced before the playlist write that adds it,
       * only the final write with #EXT-X-ENDLIST has no segment of its own */
      fail_unless (num_segments >= MIN (num_playlists, EXPECTED_SEGMENTS));
    }
    gst_message_unref (msg);
  } while (TRUE);

  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR)
    dump_error (msg);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);
  gst_message_unref (msg);

  assert_equals_int (num_segments, EXPECTED_SEGMENTS);
  /* One playlist write per segment plus the final one with #EXT-X-ENDLIST */
  assert_equals_int (num_playlists, EXPECTED_SEGMENTS + 1);

  fail_unless (g_file_get_contents (playlist_location, &playlist_content,
          NULL, NULL));
  fail_unless (g_str_has_prefix (playlist_content, "#EXTM3U"));
  fail_unless (strstr (playlist_content, "#EXT-X-ENDLIST") != NULL);

  for (i = 0; i < segment_locations->len; i++) {
    const gchar *location = g_ptr_array_index (segment_locations, i);
    gchar *basename = g_path_get_basename (location);

    expected_location = g_strdup_printf ("%s/segment%05u.ts", tmpdir, i);
    assert_equals_string (location, expected_location);
    g_free (expected_location);

    fail_unless (strstr (playlist_content, basename) != NULL,
        "Segment '%s' missing from the playlist", basename);
    g_free (basename);
  }
  g_free (playlist_content);

  assert_equals_int (count_files (tmpdir), EXPECTED_SEGMENTS + 1);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_ptr_array_free (segment_locations, TRUE);
  g_free (playlist_location);
  gst_object_unref (bus);
  gst_object_unref (src);
  gst_object_unref (sink);
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_hlssink2_segment_removed)
{
  GstElement *pipeline, *src, *sink;
  GstBus *bus;
  GstMessage *msg;
  GstCaps *caps;
  gchar *launch, *playlist_location;
  guint num_added = 0, num_removed = 0;
  guint i;

  playlist_location = g_build_filename (tmpdir, "playlist.m3u8", NULL);

  launch = g_strdup_printf ("hlssink2 name=sink target-duration=1 "
      "send-keyframe-requests=false playlist-length=0 max-files=%u "
      "location=%s/segment%%05d.ts playlist-location=%s "
      "appsrc name=src format=time ! sink.video",
      MAX_FILES_ROTATE, tmpdir, playlist_location);
  pipeline = gst_parse_launch (launch, NULL);
  fail_if (pipeline == NULL);
  g_free (launch);

  src = gst_bin_get_by_name (GST_BIN (pipeline), "src");
  fail_if (src == NULL);
  sink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");
  fail_if (sink == NULL);

  caps = gst_caps_new_simple ("video/x-h264",
      "stream-format", G_TYPE_STRING, "byte-stream",
      "alignment", G_TYPE_STRING, "au",
      "width", G_TYPE_INT, 320, "height", G_TYPE_INT, 240,
      "framerate", GST_TYPE_FRACTION, FPS, 1, NULL);
  g_object_set (src, "caps", caps, NULL);
  gst_caps_unref (caps);

  fail_if (gst_element_set_state (pipeline, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE);

  for (i = 0; i < NUM_BUFFERS_ROTATE; i++) {
    fail_unless (gst_app_src_push_buffer (GST_APP_SRC (src),
            create_buffer (i)) == GST_FLOW_OK);
  }
  fail_unless (gst_app_src_end_of_stream (GST_APP_SRC (src)) == GST_FLOW_OK);

  bus = gst_element_get_bus (pipeline);
  do {
    const GstStructure *s;

    msg = gst_bus_poll (bus,
        GST_MESSAGE_EOS | GST_MESSAGE_ERROR | GST_MESSAGE_ELEMENT, -1);
    fail_unless (msg != NULL);

    if (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_ELEMENT)
      break;

    s = gst_message_get_structure (msg);
    if (gst_structure_has_name (s, "hls-segment-added")) {
      num_added++;
    } else if (gst_structure_has_name (s, "hls-segment-removed")) {
      const gchar *location;
      gchar *expected_location;

      fail_unless (GST_MESSAGE_SRC (msg) == GST_OBJECT_CAST (sink));

      location = gst_structure_get_string (s, "location");
      fail_unless (location != NULL);

      /* Segments are rotated out oldest-first, and the file has already
       * been deleted by the time the message is posted */
      expected_location = g_strdup_printf ("%s/segment%05u.ts", tmpdir,
          num_removed);
      assert_equals_string (location, expected_location);
      g_free (expected_location);
      fail_if (g_file_test (location, G_FILE_TEST_EXISTS),
          "Removed segment '%s' still exists when the message is posted",
          location);

      num_removed++;
    }
    gst_message_unref (msg);
  } while (TRUE);

  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR)
    dump_error (msg);
  fail_unless (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_EOS);
  gst_message_unref (msg);

  assert_equals_int (num_added, EXPECTED_SEGMENTS_ROTATE);
  assert_equals_int (num_removed, EXPECTED_SEGMENTS_ROTATE - MAX_FILES_ROTATE);

  /* Only max-files segments plus the playlist remain on disk */
  assert_equals_int (count_files (tmpdir), MAX_FILES_ROTATE + 1);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_free (playlist_location);
  gst_object_unref (bus);
  gst_object_unref (src);
  gst_object_unref (sink);
  gst_object_unref (pipeline);
}

GST_END_TEST;

static gboolean
have_element (const gchar * factory_name)
{
  return gst_registry_check_feature_version (gst_registry_get (), factory_name,
      GST_VERSION_MAJOR, GST_VERSION_MINOR, 0);
}

static Suite *
hlssink2_suite (void)
{
  Suite *s = suite_create ("hlssink2");
  TCase *tc_chain = tcase_create ("general");
  gboolean have_deps;

  /* hlssink2 internally relies on splitmuxsink, mpegtsmux and giostreamsink */
  have_deps = have_element ("hlssink2") && have_element ("splitmuxsink") &&
      have_element ("mpegtsmux") && have_element ("giostreamsink") &&
      have_element ("appsrc");

  suite_add_tcase (s, tc_chain);

  if (have_deps) {
    tcase_add_checked_fixture (tc_chain, tempdir_setup, tempdir_cleanup);
    tcase_add_test (tc_chain, test_hlssink2_element_messages);
    tcase_add_test (tc_chain, test_hlssink2_segment_removed);
  } else {
    GST_INFO ("Skipping tests, missing plugins");
  }

  return s;
}

GST_CHECK_MAIN (hlssink2);
