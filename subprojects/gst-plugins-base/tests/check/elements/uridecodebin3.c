/* GStreamer
 *
 * Unit test for uridecodebin3
 *
 * Copyright (C) 2025 Loïc Le Page <llepage@igalia.com>
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
# include <config.h>
#endif

#include <gst/check/gstcheck.h>

typedef struct _TestData
{
  gchar *video_uri;
  GstElement *pipeline;
  GstElement *uridecodebin3;
  GstBus *bus;
} TestData;

static void
uridecodebin3_pad_added_cb (GstElement * uridecodebin3, GstPad * srcpad,
    GstBin * pipeline)
{
  GstElement *fakesink = gst_element_factory_make ("fakesink", NULL);
  fail_unless (fakesink != NULL);

  fail_unless (gst_bin_add (pipeline, fakesink));
  GstPad *sinkpad = gst_element_get_static_pad (fakesink, "sink");
  fail_unless (sinkpad != NULL);

  GstPadLinkReturn ret = gst_pad_link (srcpad, sinkpad);
  fail_unless (ret == GST_PAD_LINK_OK);
  gst_object_unref (sinkpad);

  fail_unless (gst_element_sync_state_with_parent (fakesink));
}

static void
test_setup (TestData * data)
{
  data->video_uri =
      g_build_filename ("file:/", GST_TEST_FILES_PATH, "theora-vorbis.ogg",
      NULL);

  data->pipeline = gst_pipeline_new ("pipeline");
  fail_unless (data->pipeline != NULL);

  data->uridecodebin3 = gst_element_factory_make ("uridecodebin3", NULL);
  fail_unless (data->uridecodebin3 != NULL);

  fail_unless (gst_bin_add (GST_BIN (data->pipeline), data->uridecodebin3));
  fail_unless (g_signal_connect (data->uridecodebin3, "pad-added",
          G_CALLBACK (uridecodebin3_pad_added_cb), data->pipeline) != 0);

  data->bus = gst_element_get_bus (data->pipeline);
  fail_unless (data->bus != NULL);
}

static void
test_teardown (TestData * data)
{
  gst_object_unref (data->bus);
  gst_element_set_state (data->pipeline, GST_STATE_NULL);
  gst_object_unref (data->pipeline);
  g_free (data->video_uri);
}

typedef void (*OnEOSActionCB) (TestData *);

static void
test_in_loops (TestData * data, int nb_loops, OnEOSActionCB cb)
{
  GstStateChangeReturn state_change =
      gst_element_set_state (data->pipeline, GST_STATE_PLAYING);
  fail_unless (state_change != GST_STATE_CHANGE_FAILURE);

  for (int i = 0; i < nb_loops; ++i) {
    GST_INFO ("Test loop %d/%d", i + 1, nb_loops);

    for (;;) {
      GstMessage *msg = gst_bus_timed_pop_filtered (data->bus, 10 * GST_SECOND,
          GST_MESSAGE_EOS | GST_MESSAGE_BUFFERING | GST_MESSAGE_ERROR);
      fail_unless (msg != NULL);

      GstMessageType type = GST_MESSAGE_TYPE (msg);
      fail_unless (type != GST_MESSAGE_ERROR);

      if (type == GST_MESSAGE_EOS) {
        GST_INFO ("EOS received");
        cb (data);
        gst_message_unref (msg);
        break;
      }

      if (type == GST_MESSAGE_BUFFERING) {
        gint percent = 0;
        gst_message_parse_buffering (msg, &percent);
        GST_INFO ("Buffering: %d%%", percent);
      }

      gst_message_unref (msg);
    }
  }
}

static void
reset_uri_after_eos_action (TestData * data)
{
  g_object_set (data->uridecodebin3, "uri", data->video_uri, NULL);
}

GST_START_TEST (test_reset_uri_after_eos)
{
  TestData data;
  test_setup (&data);
  g_object_set (data.uridecodebin3, "uri", data.video_uri, NULL);
  test_in_loops (&data, 5, reset_uri_after_eos_action);
  test_teardown (&data);
}

GST_END_TEST;

static void
seek_after_eos_action (TestData * data)
{
  fail_unless (gst_element_seek_simple (data->pipeline, GST_FORMAT_TIME,
          GST_SEEK_FLAG_ACCURATE | GST_SEEK_FLAG_FLUSH, 0));
}

GST_START_TEST (test_seek_after_eos)
{
  TestData data;
  test_setup (&data);
  g_object_set (data.uridecodebin3, "uri", data.video_uri, NULL);
  test_in_loops (&data, 5, seek_after_eos_action);
  test_teardown (&data);
}

GST_END_TEST;

static void
buffering_message_deadlock_action (TestData * data)
{
  g_object_set (data->uridecodebin3, "uri", data->video_uri, NULL);
  fail_unless (gst_element_seek_simple (data->pipeline, GST_FORMAT_TIME,
          GST_SEEK_FLAG_ACCURATE | GST_SEEK_FLAG_FLUSH, 0));
}

GST_START_TEST (test_buffering_message_deadlock)
{
  TestData data;
  test_setup (&data);
  g_object_set (data.uridecodebin3, "use-buffering", TRUE, NULL);
  g_object_set (data.uridecodebin3, "uri", data.video_uri, NULL);
  test_in_loops (&data, 5, buffering_message_deadlock_action);
  test_teardown (&data);
}

GST_END_TEST;

static Suite *
uridecodebin3_suite (void)
{
  Suite *s = suite_create ("uridecodebin3");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_reset_uri_after_eos);
  tcase_add_test (tc_chain, test_seek_after_eos);
  tcase_add_test (tc_chain, test_buffering_message_deadlock);

  return s;
}

GST_CHECK_MAIN (uridecodebin3);
