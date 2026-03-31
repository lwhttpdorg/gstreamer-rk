/* GStreamer — dynamic tee branching example
 * Copyright (C) 2026 Jonas Danielsson <jonas.danielsson@spiideo.com>
 *
 * Demonstrates gst_dynamic_add_and_link() and gst_dynamic_remove_async() by
 * growing and shrinking a set of video-sink branches attached to a tee.
 *
 * Pipeline:
 *   videotestsrc ! tee name=t
 *
 * Every two seconds a new branch is added:
 *   t. ! queue ! videoconvert ! [xvimagesink | ximagesink | fakesink]
 *
 * until MAX_SINKS (5) are present, then they are removed one by one, and the
 * cycle repeats.
 */

#include <gst/gst.h>

#define MAX_SINKS 5

static GstElement *pipeline;
static GstElement *tee;

/* Active branches, each Branch* is pushed to the tail on add and popped from
 * the head on remove. */
static GQueue active_branches = G_QUEUE_INIT;

/* Number of confirmed-live branches (updated from callbacks on main thread). */
static guint n_active = 0;

/* TRUE while we are in the "growing" phase, FALSE while shrinking. */
static gboolean growing = TRUE;

typedef struct
{
  GstPad *tee_src;              /* request pad on tee — owned by this struct */
  GstElement *queue;
  GstElement *conv;
  GstElement *sink;
  GMainLoop *loop;
} Branch;

/* --------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------- */

static GstElement *
make_sink (void)
{
  GstElement *sink;

  sink = gst_element_factory_make ("xvimagesink", NULL);
  if (sink == NULL)
    sink = gst_element_factory_make ("ximagesink", NULL);
  if (sink == NULL)
    sink = gst_element_factory_make ("fakesink", NULL);
  return sink;
}

/* --------------------------------------------------------------------------
 * Remove callback
 * -------------------------------------------------------------------------- */

static void
on_branch_removed (gboolean success, GError * error, gpointer user_data)
{
  Branch *b = user_data;
  GstBin *bin = GST_BIN (pipeline);

  if (!success) {
    g_printerr ("remove failed: %s\n", error->message);
    g_main_loop_quit (b->loop);
    g_free (b);
    return;
  }

  /* gst_bin_remove_async() tore down queue (the head element) and
   * released tee_src as a request pad.  Tear down the remaining tail
   * elements (tail-to-head: sink first, then conv). */
  gst_element_set_state (b->sink, GST_STATE_NULL);
  gst_bin_remove (bin, b->sink);
  gst_element_set_state (b->conv, GST_STATE_NULL);
  gst_bin_remove (bin, b->conv);

  g_print ("Branch '%s' removed  (total: %u)\n",
      GST_OBJECT_NAME (b->sink), n_active - 1);

  n_active--;

  /* Release the request pad reference we received from
   * gst_element_request_pad_simple(). */
  gst_object_unref (b->tee_src);
  g_free (b);

  if (n_active == 0)
    growing = TRUE;
}

/* --------------------------------------------------------------------------
 * Add / remove helpers
 * -------------------------------------------------------------------------- */

static void
add_branch (GMainLoop * loop)
{
  Branch *b;
  GstPad *queue_sink;

  b = g_new0 (Branch, 1);
  b->loop = loop;
  b->tee_src = gst_element_request_pad_simple (tee, "src_%u");
  b->queue = gst_element_factory_make ("queue", NULL);
  b->conv = gst_element_factory_make ("videoconvert", NULL);
  b->sink = make_sink ();

  if (!b->tee_src || !b->queue || !b->conv || !b->sink) {
    g_printerr ("Failed to create branch elements\n");
    g_main_loop_quit (loop);
    gst_clear_object (&b->tee_src);
    gst_clear_object (&b->queue);
    gst_clear_object (&b->conv);
    gst_clear_object (&b->sink);
    g_free (b);
    return;
  }

  g_print ("Adding branch '%s'..\n", GST_OBJECT_NAME (b->sink));

  /* Add all elements first — gst_element_link() requires a shared ancestor. */
  gst_bin_add_many (GST_BIN (pipeline), b->queue, b->conv, b->sink, NULL);
  if (!gst_element_link_many (b->queue, b->conv, b->sink, NULL)) {
    g_printerr ("Failed to link branch elements\n");
    g_main_loop_quit (loop);
    gst_element_release_request_pad (tee, b->tee_src);
    gst_object_unref (b->tee_src);
    g_free (b);
    return;
  }

  /* Sync every element deepest-first so each is ready before data arrives. */
  gst_element_sync_state_with_parent (b->sink);
  gst_element_sync_state_with_parent (b->conv);
  gst_element_sync_state_with_parent (b->queue);

  /* The tee src pad has no peer yet — no data flows through it — so
   * gst_pad_link() is safe from the application thread. */
  queue_sink = gst_element_get_static_pad (b->queue, "sink");
  if (gst_pad_link (b->tee_src, queue_sink) != GST_PAD_LINK_OK) {
    g_printerr ("Failed to link tee src to queue sink\n");
    g_main_loop_quit (loop);
    gst_object_unref (queue_sink);
    gst_element_release_request_pad (tee, b->tee_src);
    gst_object_unref (b->tee_src);
    g_free (b);
    return;
  }
  gst_object_unref (queue_sink);

  g_print ("Branch '%s' is live  (total: %u)\n",
      GST_OBJECT_NAME (b->sink), n_active + 1);

  g_queue_push_tail (&active_branches, b);
  n_active++;

  if (n_active >= MAX_SINKS)
    growing = FALSE;
}

static void
remove_oldest_branch (GMainLoop * loop)
{
  Branch *b;

  b = g_queue_pop_head (&active_branches);
  if (b == NULL)
    return;

  g_print ("Removing branch '%s'..\n", GST_OBJECT_NAME (b->sink));

  /* Pass only the head element (queue) to gst_bin_remove_async().
   * The callback tears down the remaining tail elements (conv, sink). */
  gst_bin_remove_async (GST_BIN (pipeline), b->tee_src,
      b->queue, FALSE, on_branch_removed, b);
}

/* --------------------------------------------------------------------------
 * Timer callback
 * -------------------------------------------------------------------------- */

static gboolean
timeout_cb (gpointer user_data)
{
  GMainLoop *loop = user_data;

  if (growing)
    add_branch (loop);
  else
    remove_oldest_branch (loop);

  return G_SOURCE_CONTINUE;
}

/* --------------------------------------------------------------------------
 * Bus watch
 * -------------------------------------------------------------------------- */

static gboolean
bus_cb (GstBus * bus, GstMessage * msg, gpointer user_data)
{
  GMainLoop *loop = user_data;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR:{
      GError *err = NULL;
      gchar *dbg;
      gst_message_parse_error (msg, &err, &dbg);
      gst_object_default_error (msg->src, err, dbg);
      g_clear_error (&err);
      g_free (dbg);
      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }
  return TRUE;
}

/* --------------------------------------------------------------------------
 * main
 * -------------------------------------------------------------------------- */

int
main (int argc, char **argv)
{
  GMainLoop *loop;
  GstElement *src;

  gst_init (&argc, &argv);

  pipeline = gst_pipeline_new ("pipeline");
  src = gst_element_factory_make ("videotestsrc", NULL);
  tee = gst_element_factory_make ("tee", NULL);

  if (!src || !tee) {
    g_error ("Failed to create pipeline elements");
    return 1;
  }

  g_object_set (src, "is-live", TRUE, NULL);

  /* allow-not-linked: lets the tee return GST_FLOW_OK when it has no outputs,
   * so the pipeline can start playing before any branch is attached. */
  g_object_set (tee, "allow-not-linked", TRUE, NULL);

  gst_bin_add_many (GST_BIN (pipeline), src, tee, NULL);
  gst_element_link (src, tee);

  if (gst_element_set_state (pipeline,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    g_error ("Failed to start pipeline");
    return 1;
  }

  loop = g_main_loop_new (NULL, FALSE);

  gst_bus_add_watch (GST_ELEMENT_BUS (pipeline), bus_cb, loop);
  g_timeout_add_seconds (2, timeout_cb, loop);

  g_main_loop_run (loop);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_bus_remove_watch (GST_ELEMENT_BUS (pipeline));
  gst_object_unref (pipeline);
  g_main_loop_unref (loop);

  g_queue_foreach (&active_branches, (GFunc) g_free, NULL);
  g_queue_clear (&active_branches);

  return 0;
}
