#include "gstplacebo.h"

#include "placebogl.h"

static void gst_placebo_gl_init (GstPlaceboGL * gl);
static void gst_placebo_gl_clear (GstPlaceboAPI * api);
static gboolean gst_placebo_gl_configure (GstPlaceboAPI * api);
static gboolean gst_placebo_gl_start (GstPlaceboAPI * api);
static gboolean gst_placebo_gl_stop (GstPlaceboAPI * api);
static gboolean gst_placebo_gl_ensure_element_data (GstPlaceboAPI * api);
static gboolean gst_placebo_gl_find_local_context (GstPlaceboAPI * api);
static gboolean gst_placebo_gl_handle_context_query (GstPlaceboAPI * api,
    GstPadDirection direction, GstQuery * query);
static void gst_placebo_gl_handle_set_context (GstPlaceboAPI * api,
    GstContext * context);
static gboolean gst_placebo_gl_decide_allocation (GstPlaceboAPI * api,
    GstQuery * query);
static gboolean gst_placebo_gl_set_caps (GstPlaceboAPI * api, GstCaps * in_caps,
    GstCaps * out_caps);
static gboolean gst_placebo_gl_map_frame (GstPlaceboAPI * api, GstBuffer * buf,
    struct pl_frame *frame, const GstVideoInfo * info, GstMapFlags flags);
static void gst_placebo_gl_unmap_frame (GstPlaceboAPI * api,
    struct pl_frame *frame);
static GstFlowReturn gst_placebo_gl_render (GstPlaceboAPI * api,
    struct pl_frame *frame, struct pl_frame *target);

typedef struct _GstPlaceboRenderData GstPlaceboRenderData;

#define GST_CAT_DEFAULT gst_placebo_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);
GST_DEBUG_CATEGORY_STATIC (gst_gl_debug);

#define gst_placebo_gl_parent_class parent_class
G_DEFINE_TYPE (GstPlaceboGL, gst_placebo_gl, GST_TYPE_PLACEBO_API);

static void
gst_placebo_gl_class_init (GstPlaceboGLClass * klass)
{
  GObjectClass *gobject_class;
  GstPlaceboAPIClass *api_class;

  gobject_class = (GObjectClass *) klass;
  api_class = GST_PLACEBO_API_CLASS (klass);

  api_class->clear = gst_placebo_gl_clear;
  api_class->configure = gst_placebo_gl_configure;
  api_class->start = gst_placebo_gl_start;
  api_class->stop = gst_placebo_gl_stop;
  api_class->ensure_element_data = gst_placebo_gl_ensure_element_data;
  api_class->find_local_context = gst_placebo_gl_find_local_context;
  api_class->handle_context_query = gst_placebo_gl_handle_context_query;
  api_class->handle_set_context = gst_placebo_gl_handle_set_context;
  api_class->decide_allocation = gst_placebo_gl_decide_allocation;
  api_class->set_caps = gst_placebo_gl_set_caps;
  api_class->map_frame = gst_placebo_gl_map_frame;
  api_class->unmap_frame = gst_placebo_gl_unmap_frame;
  api_class->render = gst_placebo_gl_render;

  klass->supported_gl_api =
      GST_GL_API_OPENGL | GST_GL_API_GLES2 | GST_GL_API_OPENGL3;
}

static void
gst_placebo_gl_init (GstPlaceboGL * gl)
{
  static gsize _init = 0;
  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_GET (gst_placebo_debug, "placebo");
    g_once_init_leave (&_init, 1);
  }
}

static void
gst_placebo_gl_clear (GstPlaceboAPI * api)
{
  GstPlaceboGL *gl = GST_PLACEBO_GL (api);
  g_rec_mutex_lock (&api->context_lock);
  gst_clear_object (&gl->other);
  gst_clear_object (&gl->display);
  gst_clear_object (&gl->context);
  g_rec_mutex_unlock (&api->context_lock);
}

static pl_voidfunc_t
gst_placebo_gl_proc_addr (void *ctx, const char *name)
{
  GstPlaceboGL *gl = ctx;
  pl_voidfunc_t res =
      (pl_voidfunc_t) gst_gl_context_get_proc_address (gl->context, name);
  return res;
}

static gboolean
gst_placebo_gl_configure (GstPlaceboAPI * api)
{
  GstPlaceboGL *gl = GST_PLACEBO_GL (api);
  GstDebugLevel level =
      gst_gl_debug ? gst_debug_category_get_threshold (gst_gl_debug) :
      GST_LEVEL_ERROR;
  gl->impl = pl_opengl_create (api->log,
// *INDENT-OFF*
      pl_opengl_params (
        .get_proc_addr_ex = gst_placebo_gl_proc_addr,
        .proc_ctx = gl,
        .allow_software = true,               // allow software rasterers
        .debug = level >= GST_LEVEL_WARNING,  // enable error reporting
      )
// *INDENT-ON*
//
      );
  if (!gl->impl) {
    GST_ELEMENT_ERROR (api->placebo, RESOURCE, NOT_FOUND,
        ("Failed creating OpenGL context"), (NULL));
    return FALSE;
  }
  api->gpu = gl->impl->gpu;
  return TRUE;
}

static void
_configure (GstGLContext * context, GstPlaceboAPI * api)
{
  GstPlaceboGL *gl = GST_PLACEBO_GL (api);
  static gsize _init = 0;
  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_GET (gst_gl_debug, "gldebug");
    g_once_init_leave (&_init, 1);
  }
  if (gst_placebo_api_configure (api))
    gl->status = GST_FLOW_OK;
  else
    gl->status = GST_FLOW_CUSTOM_ERROR;
}

static gboolean
gst_placebo_gl_start (GstPlaceboAPI * api)
{
  GstPlaceboGL *gl = GST_PLACEBO_GL (api);
  gst_gl_context_thread_add (gl->context,
      (GstGLContextThreadFunc) _configure, api);
  return gl->status == GST_FLOW_OK;
}

static gboolean
gst_placebo_gl_stop (GstPlaceboAPI * api)
{
  GstPlaceboGL *gl = GST_PLACEBO_GL (api);
  pl_opengl_destroy (&gl->impl);
  return TRUE;
}

static gboolean
gst_placebo_gl_ensure_element_data (GstPlaceboAPI * api)
{
  GstPlaceboGL *gl = GST_PLACEBO_GL (api);
  GstPlaceboGLClass *gl_class = GST_PLACEBO_GL_GET_CLASS (gl);
  if (!gst_gl_ensure_element_data (GST_ELEMENT (api->placebo), &gl->display,
          &gl->other))
    return FALSE;
  gst_gl_display_filter_gl_api (gl->display, gl_class->supported_gl_api);
  return TRUE;
}

static gboolean
_find_local_gl_context_unlocked (GstPlaceboGL * gl, GstPadDirection direction)
{
  GstPlaceboAPI *api = GST_PLACEBO_API (gl);
  GstGLContext *context, *tmp;
  gboolean ret;
  if (gl->context && gl->context->display == gl->display)
    return TRUE;
  context = tmp = gl->context;
  g_rec_mutex_unlock (&api->context_lock);
  /* we need to drop the lock to query as another element may also be
   * performing a context query on us which would also attempt to take the
   * context_lock. Our query could block on the same lock in the other element.
   */
  ret =
      gst_gl_query_local_gl_context (GST_ELEMENT (api->placebo), direction,
      &context);

  g_rec_mutex_lock (&api->context_lock);
  if (ret) {
    if (gl->context != tmp) {
      /* we need to recheck everything since we dropped the lock and the
       * context has changed */
      if (gl->context && gl->context->display == gl->display) {
        if (context != gl->context)
          gst_clear_object (&context);
        return TRUE;
      }
    }
    if (context->display == gl->display) {
      gl->context = context;
      return TRUE;
    }
    if (context != gl->context)
      gst_clear_object (&context);
  }
  return FALSE;
}

static gboolean
_find_local_gl_context (GstPlaceboGL * gl)
{
  GstPlaceboAPI *api = GST_PLACEBO_API (gl);
  gboolean ret;
  g_rec_mutex_lock (&api->context_lock);
  ret = _find_local_gl_context_unlocked (gl, GST_PAD_SRC);
  if (!ret)
    ret = _find_local_gl_context_unlocked (gl, GST_PAD_SINK);
  g_rec_mutex_unlock (&api->context_lock);
  return ret;
}

static gboolean
gst_placebo_gl_find_local_context_unlocked (GstPlaceboGL * gl)
{
  return (_find_local_gl_context_unlocked (gl, GST_PAD_SRC) ||
      _find_local_gl_context_unlocked (gl, GST_PAD_SINK)) &&
      gst_gl_display_ensure_context (gl->display, gl->other, &gl->context,
      NULL);
}

static gboolean
gst_placebo_gl_find_local_context (GstPlaceboAPI * api)
{
  GstPlaceboGL *gl = GST_PLACEBO_GL (api);
  gboolean ret;
  g_rec_mutex_lock (&api->context_lock);
  ret = gst_placebo_gl_find_local_context_unlocked (gl);
  g_rec_mutex_unlock (&api->context_lock);
  return ret;
}

static gboolean
gst_placebo_gl_handle_context_query (GstPlaceboAPI * api,
    GstPadDirection direction, GstQuery * query)
{
  GstPlaceboGL *gl = GST_PLACEBO_GL (api);
  GstGLDisplay *display = NULL;
  GstGLContext *other = NULL, *local = NULL;
  gboolean ret;
  g_rec_mutex_lock (&api->context_lock);
  if (gl->display)
    display = gst_object_ref (gl->display);
  if (gl->context)
    local = gst_object_ref (gl->context);
  if (gl->other)
    other = gst_object_ref (gl->other);
  g_rec_mutex_unlock (&api->context_lock);
  ret = gst_gl_handle_context_query (GST_ELEMENT (api->placebo), query,
      display, local, other);
  gst_clear_object (&display);
  gst_clear_object (&other);
  gst_clear_object (&local);
  return ret;
}

static void
_create_fbo (GstGLContext * context, GstPlaceboGL * gl)
{
  GstPlaceboAPI *api = GST_PLACEBO_API (gl);
  if (gl->fbo) {
    gst_object_unref (gl->fbo);
    gl->fbo = NULL;
  }
  gl->fbo = gst_gl_framebuffer_new_with_default_depth (context,
      GST_VIDEO_INFO_WIDTH (&api->placebo->out_info),
      GST_VIDEO_INFO_HEIGHT (&api->placebo->out_info));
}

static void
gst_placebo_gl_handle_set_context (GstPlaceboAPI * api, GstContext * context)
{
  GstPlaceboGL *gl = GST_PLACEBO_GL (api);
  GstPlaceboGLClass *gl_class = GST_PLACEBO_GL_GET_CLASS (gl);
  GstGLDisplay *old_display, *new_display;
  g_rec_mutex_lock (&api->context_lock);
  old_display = gl->display ? gst_object_ref (gl->display) : NULL;
  gst_gl_handle_set_context (GST_ELEMENT (api->placebo), context, &gl->display,
      &gl->other);
  if (gl->display)
    gst_gl_display_filter_gl_api (gl->display, gl_class->supported_gl_api);
  new_display = gl->display ? gst_object_ref (gl->display) : NULL;
  if (old_display && new_display) {
    if (old_display != new_display) {
      gst_clear_object (&gl->context);
      if (gst_placebo_gl_find_local_context_unlocked (gl))
        if (api->placebo->in_caps && api->placebo->out_caps)
          gst_gl_context_thread_add (gl->context,
              (GstGLContextThreadFunc) _create_fbo, gl);
    }
  }
  g_rec_mutex_unlock (&api->context_lock);
  gst_clear_object (&old_display);
  gst_clear_object (&new_display);
}

static gboolean
gst_placebo_gl_decide_allocation (GstPlaceboAPI * api, GstQuery * query)
{
  return gst_placebo_api_find_local_context (api);
}

static gboolean
gst_placebo_gl_set_caps (GstPlaceboAPI * api, GstCaps * in_caps,
    GstCaps * out_caps)
{
  GstPlaceboGL *gl = GST_PLACEBO_GL (api);
  GstGLTextureTarget from_target = GST_GL_TEXTURE_TARGET_NONE;
  GstGLTextureTarget to_target = GST_GL_TEXTURE_TARGET_NONE;
  GstStructure *in_s = gst_caps_get_structure (in_caps, 0);
  GstStructure *out_s = gst_caps_get_structure (out_caps, 0);
  if (gst_structure_has_field_typed (in_s, "texture-target", G_TYPE_STRING))
    from_target =
        gst_gl_texture_target_from_string (gst_structure_get_string (in_s,
            "texture-target"));
  else
    from_target = GST_GL_TEXTURE_TARGET_2D;
  if (gst_structure_has_field_typed (out_s, "texture-target", G_TYPE_STRING))
    to_target =
        gst_gl_texture_target_from_string (gst_structure_get_string (out_s,
            "texture-target"));
  else
    to_target = GST_GL_TEXTURE_TARGET_2D;
  if (to_target == GST_GL_TEXTURE_TARGET_NONE
      || from_target == GST_GL_TEXTURE_TARGET_NONE)
    /* invalid caps */
    return FALSE;
  if (gl->fbo)
    gst_object_unref (gl->fbo);
  gst_gl_context_thread_add (gl->context,
      (GstGLContextThreadFunc) _create_fbo, gl);
  return TRUE;
}

static guint
gst_placebo_fixup_texture_format (GstPlaceboGL * gl,
    const GstVideoInfo * info, guint tex_format)
{
  guint depth = GST_VIDEO_INFO_COMP_DEPTH (info, 0);
  guint type;
  switch (depth) {
    case 8:
      type = GL_UNSIGNED_BYTE;
      break;
    case 16:
      type = GL_SHORT;
      break;
    default:
      return tex_format;
  }
  return gst_gl_sized_gl_format_from_gl_format_type (gl->context, tex_format,
      type);
}

static gboolean
gst_placebo_gl_map_frame (GstPlaceboAPI * api, GstBuffer * buf,
    struct pl_frame *frame, const GstVideoInfo * info, GstMapFlags flags)
{
  GstPlaceboGL *gl = GST_PLACEBO_GL (api);
  GstVideoFrame *f = frame->user_data;
  GstMemory *mem = NULL;
  guint planes = gst_buffer_n_memory (buf);
  gst_video_frame_map (f, info, buf, flags | GST_MAP_GL);
  for (guint p = 0; p < planes; ++p) {
    mem = f->map[p].memory;
    if (G_UNLIKELY (!gst_is_gl_memory (mem)))
      return FALSE;
    GstGLMemory *gl_mem = GST_GL_MEMORY_CAST (mem);
    frame->planes[p].texture = pl_opengl_wrap (api->gpu,
// *INDENT-OFF*
        pl_opengl_wrap_params (
          .texture = gl_mem->tex_id,
          .framebuffer = gl->fbo->fbo_id,
          .width = GST_VIDEO_INFO_COMP_WIDTH (info, p),
          .height = GST_VIDEO_INFO_COMP_HEIGHT (info, p),
          .depth = 0,
          .target = gst_gl_texture_target_to_gl (gl_mem->tex_target),
          .iformat = gst_placebo_fixup_texture_format (gl, info, gl_mem->tex_format),
        )
// *INDENT-ON*
//
        );
    if (!frame->planes[p].texture)
      return FALSE;
  }
  return TRUE;
}

static void
gst_placebo_gl_unmap_frame (GstPlaceboAPI * api, struct pl_frame *frame)
{
  GstVideoFrame *f = frame->user_data;
  for (guint p = 0; p < frame->num_planes; ++p) {
    if (!frame->planes[p].texture)
      continue;
    pl_opengl_unwrap (api->gpu, frame->planes[p].texture, NULL, NULL, NULL);
  }
  gst_video_frame_unmap (f);
}

struct _GstPlaceboRenderData
{
  GstPlaceboGL *gl;
  struct pl_frame *frame, *target;
};

static void
_render (GstGLContext * context, GstPlaceboRenderData * data)
{
  GstPlaceboAPI *api = GST_PLACEBO_API (data->gl);
  GstGLFuncs *gl = data->gl->fbo->context->gl_vtable;
  guint width = 0, height = 0;
  const GLenum attachments[] = {
    GL_COLOR_ATTACHMENT0,
    GL_COLOR_ATTACHMENT1,
    GL_COLOR_ATTACHMENT2,
    GL_COLOR_ATTACHMENT3
  };

  gst_gl_framebuffer_get_effective_dimensions (data->gl->fbo, &width, &height);
  gst_gl_framebuffer_bind (data->gl->fbo);

  for (guint p = 0; p < data->target->num_planes; ++p) {
    GstVideoFrame *f = data->target->user_data;
    GstGLMemory *mem = GST_GL_MEMORY_CAST (f->map[p].memory);
    gst_gl_framebuffer_attach (data->gl->fbo,
        GL_COLOR_ATTACHMENT0 + p, (GstGLBaseMemory *) mem);
  }

  if (gl->DrawBuffers)
    gl->DrawBuffers (data->target->num_planes, attachments);
  else if (gl->DrawBuffer)
    gl->DrawBuffer (GL_COLOR_ATTACHMENT0);

  gl->Viewport (0, 0, width, height);

  if (pl_render_image (api->renderer, data->frame, data->target,
          &api->opts->params))
    data->gl->status = GST_FLOW_OK;
  else
    data->gl->status = GST_FLOW_CUSTOM_ERROR;

  if (gl->DrawBuffers)
    gl->DrawBuffers (data->target->num_planes, attachments);
  else if (gl->DrawBuffer)
    gl->DrawBuffer (GL_COLOR_ATTACHMENT0);

  /* we are done with the shader */
  if (!gst_gl_context_check_framebuffer_status (data->gl->fbo->context,
          GL_FRAMEBUFFER))
    data->gl->status = GST_FLOW_CUSTOM_ERROR;
  gst_gl_context_clear_framebuffer (data->gl->fbo->context);
}

static GstFlowReturn
gst_placebo_gl_render (GstPlaceboAPI * api, struct pl_frame *frame,
    struct pl_frame *target)
{
  GstPlaceboGL *gl = GST_PLACEBO_GL (api);
  GstPlaceboRenderData data;
  data.gl = gl;
  data.frame = frame;
  data.target = target;
  gst_gl_context_thread_add (gl->context,
      (GstGLContextThreadFunc) _render, &data);
  return gl->status;
}

GstPlaceboGL *
gst_placebo_gl_new (GstPlacebo * placebo)
{
  GstPlaceboGL *gl;

  g_return_val_if_fail (placebo != NULL, NULL);

  gl = g_object_new (GST_TYPE_PLACEBO_GL, NULL);
  GST_PLACEBO_API (gl)->placebo = placebo;

  gst_object_ref_sink (gl);

  return gl;
}
