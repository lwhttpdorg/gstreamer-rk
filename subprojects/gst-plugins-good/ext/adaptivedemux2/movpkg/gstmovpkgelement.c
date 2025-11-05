#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstmovpkgelements.h"

GST_DEBUG_CATEGORY (movpkg_debug);

void
movpkg_element_init (void)
{
  static gsize initialized = FALSE;
  if (g_once_init_enter (&initialized)) {
    GST_DEBUG_CATEGORY_INIT (movpkg_debug, "movpkg", 0, "Movpkg");
    g_once_init_leave (&initialized, TRUE);
  }
}
