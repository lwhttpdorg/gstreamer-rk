/* GStreamer
 * Copyright (C) 2026 Jonas Danielsson <jonas.danielsson@spiideo.com>
 *
 * gstdynamic.c: Tests for dynamic pipeline manipulation helpers
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

/*
 * Each test exercises one of the gst_bin_* helpers (and the recommended
 * raw-API pattern for adding branches) using only core elements (fakesrc,
 * fakesink, identity, tee, queue) that are always available.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/check/gstcheck.h>

#define PIPELINE_SETTLE_US  (100 * 1000)        /* 100 ms */
#define ASYNC_TIMEOUT_US    (5000 * 1000)       /* 5 s */

static GMutex done_mutex;
static GCond done_cond;

static GstElement *
make_element (const gchar * factory)
{
  GstElement *e = gst_element_factory_make (factory, NULL);
  fail_unless (e != NULL, "could not create element '%s'", factory);
  return e;
}

/* Block until *done is set by a callback, or the timeout is exceeded. */
static void
wait_for_done (const gboolean * done)
{
  gint64 deadline = g_get_monotonic_time () + ASYNC_TIMEOUT_US;

  g_mutex_lock (&done_mutex);
  while (!*done) {
    if (!g_cond_wait_until (&done_cond, &done_mutex, deadline))
      fail ("async operation did not complete within timeout");
  }
  g_mutex_unlock (&done_mutex);
}

static void
signal_done (gboolean * done)
{
  g_mutex_lock (&done_mutex);
  *done = TRUE;
  g_cond_signal (&done_cond);
  g_mutex_unlock (&done_mutex);
}

/* Callback data for remove tests that have tail elements to clean up.
 * Tail elements must be provided in pipeline order (head→tail); they are torn
 * down in reverse (tail→head) inside the callback. */
typedef struct
{
  gboolean done;
  GstBin *bin;
  GstElement *tail[4];
  guint n_tail;
} RemoveDoneData;

static void
on_remove_done (gboolean success, GError * error, gpointer user_data)
{
  RemoveDoneData *d = user_data;
  gint i;

  fail_unless (success);
  fail_unless (error == NULL);

  for (i = (gint) d->n_tail - 1; i >= 0; i--) {
    gst_element_set_state (d->tail[i], GST_STATE_NULL);
    gst_bin_remove (d->bin, d->tail[i]);
  }

  signal_done (&d->done);
}

/* --------------------------------------------------------------------------
 * Test: add a multi-element branch to a running tee using raw GStreamer API
 *
 * Pipeline:  fakesrc ! tee ! queue1 ! fakesink1   (already playing)
 * Add:       tee ! queue2 ! fakesink2
 *
 * Adding a branch to an idle upstream pad does not need a special helper.
 * The pattern is:
 *   1. gst_bin_add_many() — all branch elements must share an ancestor before
 *      gst_element_link() can check hierarchy.
 *   2. gst_element_link_many() — wire up the branch at NULL state.
 *   3. gst_element_sync_state_with_parent() deepest-first.
 *   4. gst_pad_link() — connect the idle tee src pad; safe from app thread
 *      because the pad has no peer yet.
 * -------------------------------------------------------------------------- */
GST_START_TEST (test_add_branch)
{
  GstElement *pipeline, *fakesrc, *tee, *queue1, *fakesink1;
  GstElement *queue2, *fakesink2;
  GstPad *tee_src, *queue2_sink;
  GstState state;

  pipeline = gst_pipeline_new (NULL);
  fakesrc = make_element ("fakesrc");
  tee = make_element ("tee");
  queue1 = make_element ("queue");
  fakesink1 = make_element ("fakesink");

  g_object_set (fakesrc, "silent", TRUE, NULL);
  g_object_set (fakesink1, "silent", TRUE, NULL);

  gst_bin_add_many (GST_BIN (pipeline), fakesrc, tee, queue1, fakesink1, NULL);
  fail_unless (gst_element_link_many (fakesrc, tee, queue1, fakesink1, NULL));

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);
  gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  g_usleep (PIPELINE_SETTLE_US);

  queue2 = make_element ("queue");
  fakesink2 = make_element ("fakesink");
  g_object_set (fakesink2, "silent", TRUE, NULL);
  /* async=FALSE: fakesink2 transitions to PLAYING without a preroll buffer. */
  g_object_set (fakesink2, "async", FALSE, NULL);

  /* Step 1: add — gst_element_link() requires a shared ancestor. */
  gst_bin_add_many (GST_BIN (pipeline), queue2, fakesink2, NULL);
  /* Step 2: link while elements are at NULL (no live pad involved). */
  fail_unless (gst_element_link (queue2, fakesink2));
  /* Step 3: sync deepest-first. */
  fail_unless (gst_element_sync_state_with_parent (fakesink2));
  fail_unless (gst_element_sync_state_with_parent (queue2));

  /* Step 4: link the idle tee src pad — safe from the application thread
   * because the pad has no peer yet. */
  tee_src = gst_element_request_pad_simple (tee, "src_%u");
  fail_unless (tee_src != NULL);
  queue2_sink = gst_element_get_static_pad (queue2, "sink");
  fail_unless_equals_int (gst_pad_link (tee_src, queue2_sink), GST_PAD_LINK_OK);
  gst_object_unref (queue2_sink);

  {
    GstElement *found;
    found = gst_bin_get_by_name (GST_BIN (pipeline), GST_OBJECT_NAME (queue2));
    fail_unless (found != NULL);
    gst_object_unref (found);

    found =
        gst_bin_get_by_name (GST_BIN (pipeline), GST_OBJECT_NAME (fakesink2));
    fail_unless (found != NULL);
    gst_object_unref (found);
  }

  gst_element_get_state (queue2, &state, NULL, GST_CLOCK_TIME_NONE);
  fail_unless_equals_int (state, GST_STATE_PLAYING);

  gst_element_get_state (fakesink1, &state, NULL, GST_CLOCK_TIME_NONE);
  fail_unless_equals_int (state, GST_STATE_PLAYING);

  gst_element_release_request_pad (tee, tee_src);
  gst_object_unref (tee_src);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
}

GST_END_TEST;

/* --------------------------------------------------------------------------
 * Test: safe remove (hard cut, drain=FALSE)
 *
 * Remove one tee branch while the pipeline keeps flowing on the other.
 * Only the head element (queue2) is passed to gst_bin_remove_async();
 * the tail element (fakesink2) is torn down in the callback.
 * -------------------------------------------------------------------------- */
GST_START_TEST (test_dynamic_remove_hard_cut)
{
  GstElement *pipeline, *fakesrc, *tee, *queue1, *fakesink1, *queue2,
      *fakesink2;
  GstPad *tee_pad, *queue2_sink;
  RemoveDoneData data = { FALSE, NULL, {NULL,}, 0 };
  GstState state;

  pipeline = gst_pipeline_new (NULL);
  fakesrc = make_element ("fakesrc");
  tee = make_element ("tee");
  queue1 = make_element ("queue");
  fakesink1 = make_element ("fakesink");
  queue2 = make_element ("queue");
  fakesink2 = make_element ("fakesink");

  g_object_set (fakesrc, "silent", TRUE, NULL);
  g_object_set (fakesink1, "silent", TRUE, NULL);
  g_object_set (fakesink2, "silent", TRUE, NULL);

  gst_bin_add_many (GST_BIN (pipeline), fakesrc, tee,
      queue1, fakesink1, queue2, fakesink2, NULL);
  fail_unless (gst_element_link_many (fakesrc, tee, queue1, fakesink1, NULL));
  fail_unless (gst_element_link_many (tee, queue2, fakesink2, NULL));

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);
  gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  g_usleep (PIPELINE_SETTLE_US);

  /* blocking_pad is the tee request pad that feeds queue2. */
  queue2_sink = gst_element_get_static_pad (queue2, "sink");
  tee_pad = gst_pad_get_peer (queue2_sink);
  fail_unless (tee_pad != NULL);
  gst_object_unref (queue2_sink);

  data.bin = GST_BIN (pipeline);
  data.tail[0] = gst_object_ref (fakesink2);
  data.n_tail = 1;

  gst_bin_remove_async (GST_BIN (pipeline), tee_pad,
      queue2, FALSE, on_remove_done, &data);

  wait_for_done (&data.done);

  /* Removed elements must no longer be in the pipeline. */
  fail_unless (gst_bin_get_by_name (GST_BIN (pipeline),
          GST_OBJECT_NAME (queue2)) == NULL);
  fail_unless (gst_bin_get_by_name (GST_BIN (pipeline),
          GST_OBJECT_NAME (fakesink2)) == NULL);

  /* Main branch must still be playing. */
  gst_element_get_state (fakesink1, &state, NULL, 0);
  fail_unless_equals_int (state, GST_STATE_PLAYING);

  gst_object_unref (tee_pad);
  gst_object_unref (data.tail[0]);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
}

GST_END_TEST;

/* --------------------------------------------------------------------------
 * Test: safe remove with EOS drain (drain=TRUE)
 *
 * identity has both sink and src pads, so the EOS probe path is exercised.
 * -------------------------------------------------------------------------- */
GST_START_TEST (test_dynamic_remove_with_drain)
{
  GstElement *pipeline, *fakesrc, *tee, *queue1, *fakesink1, *identity,
      *fakesink2;
  GstPad *tee_pad, *identity_sink;
  RemoveDoneData data = { FALSE, NULL, {NULL,}, 0 };
  GstState state;

  pipeline = gst_pipeline_new (NULL);
  fakesrc = make_element ("fakesrc");
  tee = make_element ("tee");
  queue1 = make_element ("queue");
  fakesink1 = make_element ("fakesink");
  identity = make_element ("identity");
  fakesink2 = make_element ("fakesink");

  g_object_set (fakesrc, "silent", TRUE, NULL);
  g_object_set (fakesink1, "silent", TRUE, NULL);
  g_object_set (fakesink2, "silent", TRUE, NULL);

  gst_bin_add_many (GST_BIN (pipeline), fakesrc, tee,
      queue1, fakesink1, identity, fakesink2, NULL);
  fail_unless (gst_element_link_many (fakesrc, tee, queue1, fakesink1, NULL));
  fail_unless (gst_element_link_many (tee, identity, fakesink2, NULL));

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PLAYING),
      GST_STATE_CHANGE_ASYNC);
  gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  g_usleep (PIPELINE_SETTLE_US);

  identity_sink = gst_element_get_static_pad (identity, "sink");
  tee_pad = gst_pad_get_peer (identity_sink);
  fail_unless (tee_pad != NULL);
  gst_object_unref (identity_sink);

  data.bin = GST_BIN (pipeline);
  data.tail[0] = gst_object_ref (fakesink2);
  data.n_tail = 1;

  gst_bin_remove_async (GST_BIN (pipeline), tee_pad,
      identity, TRUE, on_remove_done, &data);

  wait_for_done (&data.done);

  fail_unless (gst_bin_get_by_name (GST_BIN (pipeline),
          GST_OBJECT_NAME (identity)) == NULL);
  fail_unless (gst_bin_get_by_name (GST_BIN (pipeline),
          GST_OBJECT_NAME (fakesink2)) == NULL);

  gst_element_get_state (fakesink1, &state, NULL, 0);
  fail_unless_equals_int (state, GST_STATE_PLAYING);

  gst_object_unref (tee_pad);
  gst_object_unref (data.tail[0]);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
}

GST_END_TEST;

static Suite *
gst_dynamic_suite (void)
{
  Suite *s = suite_create ("gstdynamic");
  TCase *tc_chain = tcase_create ("general");

  tcase_set_timeout (tc_chain, 30);
  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_add_branch);
  tcase_add_test (tc_chain, test_dynamic_remove_hard_cut);
  tcase_add_test (tc_chain, test_dynamic_remove_with_drain);

  return s;
}

GST_CHECK_MAIN (gst_dynamic);
