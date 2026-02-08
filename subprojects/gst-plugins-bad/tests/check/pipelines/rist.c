#include <gst/check/check.h>
#include <gst/app/app.h>
#include <gio/gio.h>

typedef struct
{
  GstElement *pipeline;
  GstElement *src;
  GstElement *appsink;
} Receiver;

typedef struct
{
  GstElement *pipeline;
  GstElement *sink;
  GstElement *appsrc;
} Sender;


static Receiver *
create_receiver ()
{
  GstElement *depay;
  GstCaps *caps;
  Receiver *receiver = g_new0 (Receiver, 1);
  receiver->pipeline = gst_pipeline_new (NULL);

  caps = gst_caps_new_simple ("application/x-rtp",
      "media", G_TYPE_STRING, "application",
      "clock-rate", G_TYPE_INT, 90000,
      "encoding-name", G_TYPE_STRING, "X-GST", NULL);
  g_assert_nonnull (caps);

  receiver->src = gst_element_factory_make_full ("ristsrc", "caps", caps, NULL);
  g_assert_nonnull (receiver->src);
  gst_caps_unref (caps);

  depay = gst_element_factory_make ("rtpgstdepay", NULL);
  g_assert_nonnull (depay);

  receiver->appsink = gst_element_factory_make ("appsink", NULL);
  g_assert_nonnull (receiver->appsink);

  gst_bin_add_many (GST_BIN (receiver->pipeline), receiver->src, depay,
      receiver->appsink, NULL);
  fail_unless (gst_element_link_many (receiver->src, depay, receiver->appsink,
          NULL));

  return receiver;
}

static void
cleanup_receiver (Receiver * receiver)
{
  GstStateChangeReturn state_change;

  state_change = gst_element_set_state (receiver->pipeline, GST_STATE_NULL);
  fail_unless_equals_int (state_change, GST_STATE_CHANGE_SUCCESS);

  gst_object_unref (receiver->pipeline);
  g_free (receiver);
}

static Sender *
create_sender ()
{
  GstElement *pay;
  GstCaps *caps;
  Sender *sender = g_new0 (Sender, 1);
  sender->pipeline = gst_pipeline_new (NULL);

  sender->appsrc = gst_element_factory_make ("appsrc", NULL);
  g_assert_nonnull (sender->appsrc);
  pay = gst_element_factory_make ("rtpgstpay", NULL);
  g_assert_nonnull (pay);
  sender->sink = gst_element_factory_make_full ("ristsink",
      "address", "127.0.0.1", NULL);
  g_assert_nonnull (sender->sink);

  caps =
      gst_caps_new_simple ("application/testing",
      "clock-rate", G_TYPE_INT, 90000, NULL);
  gst_app_src_set_caps (GST_APP_SRC (sender->appsrc), caps);
  gst_caps_unref (caps);
  g_object_set (G_OBJECT (sender->appsrc), "format", GST_FORMAT_TIME, NULL);

  gst_bin_add_many (GST_BIN (sender->pipeline), sender->appsrc, pay,
      sender->sink, NULL);
  fail_unless (gst_element_link_many (sender->appsrc, pay, sender->sink, NULL));

  return sender;
}

static void
cleanup_sender (Sender * sender)
{
  GstStateChangeReturn state_change;

  state_change = gst_element_set_state (sender->pipeline, GST_STATE_NULL);
  fail_unless_equals_int (state_change, GST_STATE_CHANGE_SUCCESS);

  gst_object_unref (sender->pipeline);
  g_free (sender);
}

static void
configure_addresses (Receiver * receiver, Sender * sender, gboolean use_bonding,
    gboolean use_ipv6, int count)
{
  int i, j;
  guint port;
  GString *addr;
  GstStateChangeReturn state_change;

  // Since we can't auto bind, try to find a valid port
  for (i = 0; i < 5; ++i) {
    port = g_random_int_range (10000, 60000) & 0xfffe;
    if (!use_bonding) {
      g_object_set (receiver->src, "port", port, NULL);
    } else {
      addr = g_string_sized_new (16 * count);
      for (j = 0; j < count; ++j) {
        g_string_append_printf (addr, "%s%s:%u", j == 0 ? "" : ", ",
            (use_ipv6 && j & 0x01) ? "::" : "0.0.0.0", port + (j * 2));
      }
      g_object_set (receiver->src, "bonding-addresses", addr->str, NULL);
      g_string_free (addr, TRUE);
    }

    state_change =
        gst_element_set_state (receiver->pipeline, GST_STATE_PLAYING);
    if (state_change != GST_STATE_CHANGE_FAILURE)
      break;
  }
  fail_unless (state_change != GST_STATE_CHANGE_FAILURE);

  if (!use_bonding) {
    g_object_set (sender->sink, "port", port, NULL);
  } else {
    addr = g_string_sized_new (16 * count);
    for (j = 0; j < count; ++j) {
      g_string_append_printf (addr, "%s%s:%u", j == 0 ? "" : ", ",
          (use_ipv6 && j & 0x01) ? "::1" : "127.0.0.1", port + (j * 2));
    }
    g_object_set (sender->sink, "bonding-addresses", addr->str, NULL);
    g_string_free (addr, TRUE);
  }

  // g_object_set (sender->sink, "bonding-method", 1, NULL);
  state_change = gst_element_set_state (sender->pipeline, GST_STATE_PLAYING);
  fail_unless (state_change != GST_STATE_CHANGE_FAILURE);
}

static void
check_transfer (Receiver * receiver, Sender * sender, int size)
{
  gchar *compare;
  GstFlowReturn flow;
  GstBuffer *buffer;
  GstSample *sample;
  int i;

  compare = g_malloc (size);

  for (i = 0; i < 16; ++i) {
    buffer = gst_buffer_new_allocate (NULL, size, NULL);
    g_assert_nonnull (buffer);
    gst_buffer_memset (buffer, 0, 0x30 + i, size);
    memset (compare, 0x30 + i, size);

    flow = gst_app_src_push_buffer (GST_APP_SRC (sender->appsrc), buffer);
    fail_unless_equals_int (flow, GST_FLOW_OK);

    sample =
        gst_app_sink_try_pull_sample (GST_APP_SINK (receiver->appsink),
        5 * GST_SECOND);
    fail_unless (sample != NULL);

    buffer = gst_sample_get_buffer (sample);
    fail_unless (buffer != NULL);
    fail_unless (gst_buffer_get_size (buffer) == size);
    fail_unless (gst_buffer_memcmp (buffer, 0, compare, size) == 0);

    gst_sample_unref (sample);
  }

  g_free (compare);
}

static void
run_basic_test (gboolean use_bonding, gboolean use_ipv6, int count)
{
  Receiver *receiver;
  Sender *sender;

  receiver = create_receiver ();
  sender = create_sender ();

  configure_addresses (receiver, sender, use_bonding, use_ipv6, count);

  check_transfer (receiver, sender, 32);
  check_transfer (receiver, sender, 1024);
  check_transfer (receiver, sender, 8192);

  cleanup_receiver (receiver);
  cleanup_sender (sender);
}

GST_START_TEST (test_single)
{
  run_basic_test (FALSE, FALSE, 1);
}

GST_END_TEST;

GST_START_TEST (test_single_bonding)
{
  run_basic_test (TRUE, FALSE, 1);
}

GST_END_TEST;

GST_START_TEST (test_bonding)
{
  run_basic_test (TRUE, FALSE, 3);
}

GST_END_TEST;

GST_START_TEST (test_single_ipv6)
{
  run_basic_test (FALSE, TRUE, 1);
}

GST_END_TEST;

GST_START_TEST (test_single_bonding_ipv6)
{
  run_basic_test (TRUE, TRUE, 1);
}

GST_END_TEST;

GST_START_TEST (test_bonding_ipv6)
{
  run_basic_test (TRUE, TRUE, 3);
}

GST_END_TEST;


static gboolean
is_ipv6_supported (void)
{
  GError *err = NULL;
  GSocket *sock;

  sock =
      g_socket_new (G_SOCKET_FAMILY_IPV6, G_SOCKET_TYPE_DATAGRAM,
      G_SOCKET_PROTOCOL_DEFAULT, &err);
  if (sock) {
    g_object_unref (sock);
    return TRUE;
  }

  if (!g_error_matches (err, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED)) {
    GST_WARNING ("Unabled to create IPv6 socket: %s", err->message);
  }
  g_clear_error (&err);

  return FALSE;
}

static Suite *
rist_suite (void)
{
  Suite *s = suite_create ("rist");
  TCase *tc_chain = tcase_create ("general");
  gboolean have_ipv6 = is_ipv6_supported ();

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_single);
  tcase_add_test (tc_chain, test_single_bonding);
  tcase_add_test (tc_chain, test_bonding);
  if (have_ipv6) {
    tcase_add_test (tc_chain, test_single_ipv6);
    tcase_add_test (tc_chain, test_single_bonding_ipv6);
    tcase_add_test (tc_chain, test_bonding_ipv6);
  }

  return s;
}

GST_CHECK_MAIN (rist);
