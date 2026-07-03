#include <gst/gstdebugutils.h>
#include <gst/gstinfo.h>

#include "placeboapi.h"

#define GST_CAT_DEFAULT gst_placebo_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define gst_placebo_api_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE (GstPlaceboAPI, gst_placebo_api, GST_TYPE_OBJECT);

static void gst_placebo_api_finalize (GObject * object);

static void
gst_placebo_api_class_init (GstPlaceboAPIClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = gst_placebo_api_finalize;
}

static void
gst_placebo_log (void *data, enum pl_log_level level, const char *msg)
{
  GstPlaceboAPI *api = GST_PLACEBO_API (data);
  GstDebugLevel threshold = GST_LEVEL_NONE;
  switch (level) {
    case PL_LOG_FATAL:
      G_GNUC_FALLTHROUGH;
    case PL_LOG_ERR:
      threshold = GST_LEVEL_ERROR;
      break;
    case PL_LOG_WARN:
      threshold = GST_LEVEL_WARNING;
      break;
    case PL_LOG_TRACE:
      threshold = GST_LEVEL_TRACE;
      break;
    default:
      threshold = (GstDebugLevel) level;
      break;
  }
  GST_CAT_LEVEL_LOG (GST_CAT_DEFAULT, threshold, api->placebo, "%s", msg);
}

static void
gst_placebo_renderer_info (void *priv, const struct pl_render_info *info)
{
  GstPlaceboAPI *api = GST_PLACEBO_API (priv);
  GST_LOG_OBJECT (api->placebo, "shader: %s", info->pass->shader->description);
}

static void
gst_placebo_api_init (GstPlaceboAPI * api)
{
  static gsize _init = 0;
  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_GET (gst_placebo_debug, "placebo");
    g_once_init_leave (&_init, 1);
  }

  enum pl_log_level level = PL_LOG_NONE;
  GstDebugLevel threshold =
      gst_debug_category_get_threshold (gst_placebo_debug);

  switch (threshold) {
    case GST_LEVEL_ERROR:
      level = PL_LOG_ERR;
      break;
    case GST_LEVEL_FIXME:
      G_GNUC_FALLTHROUGH;
    case GST_LEVEL_WARNING:
      level = PL_LOG_WARN;
      break;
    case GST_LEVEL_LOG:
      level = PL_LOG_DEBUG;
      break;
    case GST_LEVEL_TRACE:
      level = PL_LOG_TRACE;
      break;
    default:
      level = (enum pl_log_level) threshold;
      break;
  }

  api->log = pl_log_create (PL_API_VER, pl_log_params (.log_cb =
          gst_placebo_log,.log_priv = api,.log_level = level,));
  api->opts = pl_options_alloc (api->log);
  api->opts->params = pl_render_default_params;
  api->opts->params.info_callback = gst_placebo_renderer_info;
  api->opts->params.info_priv = api;
}

static void
gst_placebo_api_finalize (GObject * object)
{
  GstPlaceboAPI *api = GST_PLACEBO_API (object);

  if (api->source_hook)
    pl_mpv_user_shader_destroy (&api->source_hook);
  if (api->location_hook)
    pl_mpv_user_shader_destroy (&api->location_hook);
  pl_options_free (&api->opts);
  pl_log_destroy (&api->log);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

void
gst_placebo_api_clear (GstPlaceboAPI * api)
{
  GST_PLACEBO_API_GET_CLASS (api)->clear (api);
}

gboolean
gst_placebo_api_configure (GstPlaceboAPI * api)
{
  if (GST_PLACEBO_API_GET_CLASS (api)->configure (api))
    api->renderer = pl_renderer_create (api->log, api->gpu);
  return api->renderer != NULL;
}

gboolean
gst_placebo_api_start (GstPlaceboAPI * api)
{
  return GST_PLACEBO_API_GET_CLASS (api)->start (api);
}

gboolean
gst_placebo_api_stop (GstPlaceboAPI * api)
{
  pl_renderer_destroy (&api->renderer);
  return GST_PLACEBO_API_GET_CLASS (api)->stop (api);
}

gboolean
gst_placebo_api_ensure_element_data (GstPlaceboAPI * api)
{
  return GST_PLACEBO_API_GET_CLASS (api)->ensure_element_data (api);
}

gboolean
gst_placebo_api_find_local_context (GstPlaceboAPI * api)
{
  return GST_PLACEBO_API_GET_CLASS (api)->find_local_context (api);
}

gboolean
gst_placebo_api_handle_context_query (GstPlaceboAPI * api,
    GstPadDirection direction, GstQuery * query)
{
  return GST_PLACEBO_API_GET_CLASS (api)->handle_context_query (api, direction,
      query);
}

void
gst_placebo_api_handle_set_context (GstPlaceboAPI * api, GstContext * context)
{
  GST_PLACEBO_API_GET_CLASS (api)->handle_set_context (api, context);
}

gboolean
gst_placebo_api_decide_allocation (GstPlaceboAPI * api, GstQuery * query)
{
  return GST_PLACEBO_API_GET_CLASS (api)->decide_allocation (api, query);
}

gboolean
gst_placebo_api_set_caps (GstPlaceboAPI * api, GstCaps * in_caps,
    GstCaps * out_caps)
{
  return GST_PLACEBO_API_GET_CLASS (api)->set_caps (api, in_caps, out_caps);
}

gboolean
gst_placebo_api_map_frame (GstPlaceboAPI * api, GstBuffer * buf,
    struct pl_frame *frame, const GstVideoInfo * info, GstMapFlags flags)
{
  return GST_PLACEBO_API_GET_CLASS (api)->map_frame (api, buf, frame, info,
      flags);
}

void
gst_placebo_api_unmap_frame (GstPlaceboAPI * api, struct pl_frame *frame)
{
  GST_PLACEBO_API_GET_CLASS (api)->unmap_frame (api, frame);
  for (guint p = 0; p < 4; ++p) {
    if (!frame->planes[p].texture)
      continue;
    pl_tex_destroy (api->gpu, &frame->planes[p].texture);
  }
}

GstFlowReturn
gst_placebo_api_render (GstPlaceboAPI * api, struct pl_frame *frame,
    struct pl_frame *target)
{
  return GST_PLACEBO_API_GET_CLASS (api)->render (api, frame, target);
}
