/*
 * placebo gstreamer plugin
 * Copyright (C) 2025 Martin Rodriguez Reboredo <yakoyoku@gmail.com>
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

#ifndef _GST_PLACEBO_GL_H_
#define _GST_PLACEBO_GL_H_

#include <gst/gl/gl.h>
#include <gst/gl/gstglfuncs.h>

#include <libplacebo/opengl.h>

#include "placeboapi.h"

typedef struct _GstPlaceboGL GstPlaceboGL;
typedef struct _GstPlaceboGLClass GstPlaceboGLClass;

#define GST_TYPE_PLACEBO_GL            (gst_placebo_gl_get_type())
#define GST_PLACEBO_GL(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_PLACEBO_GL,GstPlaceboGL))
#define GST_IS_PLACEBO_GL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_PLACEBO_GL))
#define GST_PLACEBO_GL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_PLACEBO_GL,GstPlaceboGLClass))
#define GST_IS_PLACEBO_GL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_PLACEBO_GL))
#define GST_PLACEBO_GL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_PLACEBO_GL,GstPlaceboGLClass))

struct _GstPlaceboGL
{
  GstPlaceboAPI api;

  GstGLContext *context;
  GstGLDisplay *display;
  GstGLFramebuffer *fbo;
  GstGLContext *other;
  pl_opengl impl;

  GstFlowReturn status;
};

struct _GstPlaceboGLClass
{
  GstPlaceboAPIClass api_class;

  guint supported_gl_api;
};

GType gst_placebo_gl_get_type ();

GstPlaceboGL * gst_placebo_gl_new (GstPlacebo * placebo);

#endif /* _GST_PLACEBO_GL_H_ */
