/*
 * GStreamer
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
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
#include "config.h"
#endif

#include "gstqt6elements.h"
#ifdef HAVE_QT_GL
#include "qt6glitem.h"
#endif
#include "qt6switem.h"
#include <QtQml/QQmlApplicationEngine>

void
qt6_element_init (GstPlugin * plugin)
{
  static gsize res = FALSE;
  if (g_once_init_enter (&res)) {
    /* this means the plugin must be loaded before the qml engine is loaded */
    #ifdef HAVE_QT_GL
    qmlRegisterType<Qt6GLVideoItem> ("org.freedesktop.gstreamer.Qt6GLVideoItem", 1, 0, "GstGLQt6VideoItem");
    #endif
    qmlRegisterType<Qt6SWVideoItem> ("org.freedesktop.gstreamer.Qt6SWVideoItem", 1, 0, "GstSWQt6VideoItem");
    g_once_init_leave (&res, TRUE);
  }
}
