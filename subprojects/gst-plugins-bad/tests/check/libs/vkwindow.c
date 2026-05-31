/* GStreamer
 *
 * Copyright (C) 2019 Matthew Waters <matthew@centricular.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst/vulkan/vulkan.h>
#include <gst/vulkan/xcb/gstvkdisplay_xcb.h>
#include "../../gst-libs/gst/vulkan/xcb/gstvkwindow_xcb.h"

static GstVulkanDisplay *display;
static GstVulkanInstance *instance;

static void
setup (void)
{
  instance = gst_vulkan_instance_new ();
  fail_unless (gst_vulkan_instance_open (instance, NULL));
  display = gst_vulkan_display_new (instance);
}

static void
teardown (void)
{
  gst_object_unref (display);
  gst_object_unref (instance);
}

GST_START_TEST (test_window_new)
{
  GstVulkanWindow *window;
  GstVulkanDisplay *win_display;

  window = gst_vulkan_window_new (display);
  g_object_get (window, "display", &win_display, NULL);
  fail_unless (win_display == display);
  gst_object_unref (win_display);
  gst_object_unref (window);
}

GST_END_TEST;

GST_START_TEST (test_window_xcb_initial_size)
{
#if GST_VULKAN_HAVE_WINDOW_XCB
  GstVulkanDisplayXCB *xcb_display;
  GstVulkanWindow *window;
  xcb_get_geometry_cookie_t cookie;
  xcb_get_geometry_reply_t *reply;

  xcb_display = gst_vulkan_display_xcb_new (NULL);
  if (!xcb_display)
    return;

  window = gst_vulkan_display_create_window (GST_VULKAN_DISPLAY (xcb_display));
  fail_unless (window != NULL);
  gst_vulkan_window_resize (window, 1920, 1080);
  fail_unless (gst_vulkan_window_open (window, NULL));

  cookie = xcb_get_geometry (GST_VULKAN_DISPLAY_XCB_CONNECTION (xcb_display),
      ((GstVulkanWindowXCB *) window)->win_id);
  reply =
      xcb_get_geometry_reply (GST_VULKAN_DISPLAY_XCB_CONNECTION (xcb_display),
      cookie, NULL);

  fail_unless (reply != NULL);
  fail_unless_equals_int (reply->width, 1920);
  fail_unless_equals_int (reply->height, 1080);

  free (reply);
  gst_vulkan_window_close (window);
  gst_object_unref (window);
  gst_object_unref (xcb_display);
#endif
}

GST_END_TEST;

static Suite *
vkwindow_suite (void)
{
  Suite *s = suite_create ("vkwindow");
  TCase *tc_basic = tcase_create ("general");
  gboolean have_instance;

  suite_add_tcase (s, tc_basic);
  tcase_add_checked_fixture (tc_basic, setup, teardown);

  instance = gst_vulkan_instance_new ();
  have_instance = gst_vulkan_instance_open (instance, NULL);
  gst_object_unref (instance);
  if (have_instance) {
    tcase_add_test (tc_basic, test_window_new);
    tcase_add_test (tc_basic, test_window_xcb_initial_size);
  }

  return s;
}

#ifdef __APPLE__
#if TARGET_OS_OSX
static int
run_tests ()
{
  Suite *s = vkwindow_suite ();
  return gst_check_run_suite_nofork (s, "vkwindow", __FILE__);
}

int
main (int argc, char **argv)
{
  /* gst_macos_main() is needed to setup an NSApplication,
   * otherwise this test will print a critical warning and fail on macOS */
  gst_check_init (&argc, &argv);
  return gst_macos_main_simple ((GstMainFuncSimple) run_tests, NULL);
}
#else
GST_CHECK_MAIN_NOFORK (vkwindow);
#endif
#else
GST_CHECK_MAIN (vkwindow);
#endif
