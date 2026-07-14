/*
 * Sanity: load the plugin, look up both element factories, exit 0 on success.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include <gst/gst.h>

int main(int argc, char *argv[])
{
  gst_init(&argc, &argv);

  GstElementFactory *opus = gst_element_factory_find("c2opusdec");
  GstElementFactory *aac  = gst_element_factory_find("c2aacdec");

  if (!opus) { g_printerr("c2opusdec factory not found\n"); return 1; }
  if (!aac)  { g_printerr("c2aacdec factory not found\n");  return 1; }

  g_print("OK: both c2opusdec and c2aacdec registered.\n");
  g_object_unref(opus);
  g_object_unref(aac);
  gst_deinit();
  return 0;
}
