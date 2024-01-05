/* GStreamer
 *
 *  Copyright 2024 NXP
 *   @author: Elliot Chen <elliot.chen@nxp.com>
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

#include <string.h>

#include "gstautoaudioconvert.h"

GST_DEBUG_CATEGORY (autoaudioconvert_debug);
#define GST_CAT_DEFAULT (autoaudioconvert_debug)

#define GST_AUTOAUDIOSELECT_LOCK(ac) GST_OBJECT_LOCK (ac)
#define GST_AUTOAUDIOSELECT_UNLOCK(ac) GST_OBJECT_UNLOCK (ac)

/* element factory information */
static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

enum
{
  PROP_0,
  PROP_PERFERRED_FACTORY_ORDER
};

static void gst_auto_audio_convert_dispose (GObject * object);
static GList *gst_auto_audio_convert_create_factory_list (GstAutoAudioConvert
    * autoaudioconvert);
static void gst_auto_audio_convert_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_auto_audio_convert_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

G_DEFINE_TYPE (GstAutoAudioConvert, gst_auto_audio_convert,
    GST_TYPE_AUTO_SELECT);
GST_ELEMENT_REGISTER_DEFINE (autoaudioconvert, "autoaudioconvert",
    GST_RANK_NONE, GST_TYPE_AUTO_AUDIO_CONVERT);

static void
gst_auto_audio_convert_class_init (GstAutoAudioConvertClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  GST_DEBUG_CATEGORY_INIT (autoaudioconvert_debug, "autoaudioconvert", 0,
      "Auto audio converter");

  gst_element_class_add_static_pad_template (gstelement_class, &srctemplate);
  gst_element_class_add_static_pad_template (gstelement_class, &sinktemplate);

  gst_element_class_set_static_metadata (gstelement_class,
      "Select or combine audio converters based on caps", "Generic/Bin",
      "Select or combine the right audio converters based on the caps",
      "Elliot Chen <elliot.chen@nxp.com>");
  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_auto_audio_convert_dispose);
  gobject_class->get_property = gst_auto_audio_convert_get_property;
  gobject_class->set_property = gst_auto_audio_convert_set_property;

  g_object_class_install_property (gobject_class, PROP_PERFERRED_FACTORY_ORDER,
      g_param_spec_string ("perferred-factory-order",
          "perferred factory order",
          "user can configure the perferred factory name order at the start"
          " and it can be used to reorder the GList of auto-discovered factories,"
          " example: perferred-factory-order=audioresample,audioconvert",
          NULL, G_PARAM_READWRITE));
}

static void
gst_auto_audio_convert_init (GstAutoAudioConvert * autoaudioconvert)
{
  GList *factories = NULL;

  autoaudioconvert->perferred_factory_order = NULL;
  factories = gst_auto_audio_convert_create_factory_list (autoaudioconvert);
  g_object_set (GST_ELEMENT (autoaudioconvert), "factories", factories, NULL);

  g_free (factories);
  return;
}

static void
gst_auto_audio_convert_dispose (GObject * object)
{
  GstAutoAudioConvert *autoaudioconvert = GST_AUTO_AUDIO_CONVERT (object);

  g_free (autoaudioconvert->perferred_factory_order);

  G_OBJECT_CLASS (gst_auto_audio_convert_parent_class)->dispose (object);
}

static gboolean
gst_auto_audio_convert_element_filter (GstPluginFeature * feature,
    GstAutoAudioConvert * autoaudioconvert)
{
  const gchar *klass;

  /* we only care about element factories */
  if (G_UNLIKELY (!GST_IS_ELEMENT_FACTORY (feature)))
    return FALSE;

  klass = gst_element_factory_get_metadata (GST_ELEMENT_FACTORY_CAST (feature),
      GST_ELEMENT_METADATA_KLASS);

  if (strstr (klass, "Filter") &&
      strstr (klass, "Converter") && strstr (klass, "Audio")) {
    GST_DEBUG_OBJECT (autoaudioconvert,
        "gst_auto_audio_convert_element_filter found %s",
        gst_plugin_feature_get_name (GST_PLUGIN_FEATURE_CAST (feature)));
    return TRUE;
  }

  return FALSE;
}

static GList *
gst_auto_audio_convert_create_factory_list (GstAutoAudioConvert *
    autoaudioconvert)
{
  GList *result = NULL;

  /* get the feature list using the filter */
  result = gst_registry_feature_filter (gst_registry_get (),
      (GstPluginFeatureFilter) gst_auto_audio_convert_element_filter,
      FALSE, autoaudioconvert);

  /* sort on rank and name */
  result = g_list_sort (result, gst_plugin_feature_rank_compare_func);

  return result;
}

static gboolean
gst_auto_audio_convert_update_factory_list (GstAutoAudioConvert *
    autoaudioconvert, gchar * perferred_factory_order)
{
  gchar **factories_str = NULL;
  gchar **factory_name = NULL;
  GList *factories = NULL;
  GList *curr = NULL;
  GList *result = NULL;

  /* Check the perferred factory name */
  if (!perferred_factory_order) {
    GST_DEBUG_OBJECT (autoaudioconvert, "The perferred factory name is NULL");
    return FALSE;
  } else {
    factories_str = g_strsplit (perferred_factory_order, ",", -1);
    if (!(*factories_str)) {
      GST_DEBUG_OBJECT (autoaudioconvert,
          "Can't get valid perferred factory name");
      return FALSE;
    }
    g_free (autoaudioconvert->perferred_factory_order);
    autoaudioconvert->perferred_factory_order = perferred_factory_order;
  }

  /* Get the currnet factory list */
  g_object_get (GST_ELEMENT (autoaudioconvert), "factories", &factories, NULL);
  if (!factories) {
    GST_DEBUG_OBJECT (autoaudioconvert, "The factory list is NULL");
    return FALSE;
  } else {
    factories = g_list_copy (factories);
  }

  /* reorder the factory list */
  for (factory_name = factories_str; *factory_name; factory_name++) {
    for (curr = factories; curr; curr = g_list_next (curr)) {
      GstElementFactory *factory = GST_ELEMENT_FACTORY (curr->data);

      if (strcmp (*factory_name,
              gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory)))) {
        continue;
      }
      factories = g_list_remove_link (factories, curr);
      result = g_list_concat (result, curr);
      GST_DEBUG_OBJECT (autoaudioconvert, "reorder list, factory name %s",
          gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory)));
      break;
    }
  }
  result = g_list_concat (result, factories);
  /* Update the factory list */
  g_object_set (GST_ELEMENT (autoaudioconvert), "factories", result, NULL);

  if (result)
    g_list_free (result);
  g_strfreev (factories_str);
  return TRUE;
}

static void
gst_auto_audio_convert_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstState cur_state;
  GstAutoAudioConvert *autoaudioconvert = GST_AUTO_AUDIO_CONVERT (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    case PROP_PERFERRED_FACTORY_ORDER:
      gst_element_get_state (GST_ELEMENT (autoaudioconvert), &cur_state, NULL,
          0);
      if (cur_state == GST_STATE_NULL) {
        gst_auto_audio_convert_update_factory_list (autoaudioconvert,
            g_value_dup_string (value));
      } else {
        GST_WARNING_OBJECT (object, "Can not set perferred factory order"
            "because element is not in the NULL state or initial state");
      }
      break;
  }
}

static void
gst_auto_audio_convert_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstAutoAudioConvert *autoaudioconvert = GST_AUTO_AUDIO_CONVERT (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    case PROP_PERFERRED_FACTORY_ORDER:
      g_value_set_string (value, autoaudioconvert->perferred_factory_order);
      break;
  }
}
