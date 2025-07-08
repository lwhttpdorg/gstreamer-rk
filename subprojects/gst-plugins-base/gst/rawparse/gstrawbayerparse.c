/* GStreamer
 * Copyright (C) <2016> Carlos Rafael Giani <dv at pseudoterminal dot org>
 * Copyright (C) <2017> Vincent Penquerc'h <vincent at collabora dot co dot uk>
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

/**
 * SECTION:element-rawbayerparse
 * @title: rawbayerparse
 *
 * This element parses incoming data as raw video frames and timestamps these.
 * It also handles seek queries in said raw video data, and ensures that output
 * buffers contain exactly one frame, even if the input buffers contain only
 * partial frames or multiple frames. In the former case, it will continue to
 * receive buffers until there is enough input data to output one frame. In the
 * latter case, it will extract the first frame in the buffer and output it, then
 * the second one etc. until the remaining unparsed bytes aren't enough to form
 * a complete frame, and it will then continue as described in the earlier case.
 *
 * The element implements the properties and sink caps configuration as specified
 * in the #GstRawBaseParse documentation. The properties configuration can be
 * modified by using the width, height, pixel-aspect-ratio, framerate, and
 * frame-size properties.
 *
 * The frame size property is useful in cases where there is extra data between
 * the frames (for example, trailing metadata, or headers). The parser calculates
 * the actual frame size out of the other properties and compares it with this
 * frame-size value. If the frame size is larger than the calculated size,
 * then the extra bytes after the end of the frame are skipped. For example, with
 * 8-bit grayscale frames and a actual frame size of 100x10 pixels and a frame-size of
 * 1500 bytes, there are 500 excess bytes at the end of the actual frame which
 * are then skipped. It is safe to set the frame size to a value that is smaller
 * than the actual frame size (in fact, its default value is 0); if it is smaller,
 * then no trailing data will be skipped.
 *
 * If a framerate of 0 Hz is set (for example, 0/1), then output buffers will have
 * no duration set. The first output buffer will have a PTS 0, all subsequent ones
 * an unset PTS.
 *
 * ## Example pipelines
 * |[
 * gst-launch-1.0 filesrc location=video.raw ! rawbayerparse use-sink-caps=false \
 *         width=500 height=400 format=grbg ! autovideosink
 * ]|
 *  Read raw data from a local file and parse it as video data with 500x400 pixels
 * and GRGB Bayer format.
 * |[
 * gst-launch-1.0 filesrc location=video.raw ! queue ! "video/x-bayer, width=320, \
 *         height=240, format=grbg, framerate=1/1" ! rawbayerparse \
 *         use-sink-caps=true ! autovideosink
 * ]|
 *  Read raw data from a local file and parse it as video data with 320x240 pixels
 * and GRBG Bayer format. The queue element here is to force push based scheduling.
 * See the documentation in #GstRawBaseParse for the reason why.
 *
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include "gstrawparseelements.h"
#include "gstrawbayerparse.h"
#include "unalignedvideo.h"

GST_DEBUG_CATEGORY_STATIC (raw_bayer_parse_debug);
#define GST_CAT_DEFAULT raw_bayer_parse_debug

enum
{
  PROP_0,
  PROP_WIDTH,
  PROP_HEIGHT,
  PROP_FORMAT,
  PROP_PIXEL_ASPECT_RATIO,
  PROP_FRAMERATE,
  PROP_FRAME_SIZE
};

#define DEFAULT_WIDTH                 320
#define DEFAULT_HEIGHT                240
#define DEFAULT_FORMAT                GST_RAW_BAYER_PARSE_FORMAT_BGGR
#define DEFAULT_PIXEL_ASPECT_RATIO_N  1
#define DEFAULT_PIXEL_ASPECT_RATIO_D  1
#define DEFAULT_FRAMERATE_N           25
#define DEFAULT_FRAMERATE_D           1
#define DEFAULT_FRAME_STRIDE          0

static GstStaticPadTemplate static_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_UNALIGNED_RAW_VIDEO_CAPS "; video/x-bayer")
    );

static GstStaticPadTemplate static_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-bayer")
    );

#define gst_raw_bayer_parse_parent_class parent_class
G_DEFINE_TYPE (GstRawBayerParse, gst_raw_bayer_parse, GST_TYPE_RAW_BASE_PARSE);
GST_ELEMENT_REGISTER_DEFINE (rawbayerparse, "rawbayerparse",
    GST_RANK_NONE, GST_TYPE_RAW_BAYER_PARSE);

static void gst_raw_bayer_parse_set_property (GObject * object, guint prop_id,
    GValue const *value, GParamSpec * pspec);
static void gst_raw_bayer_parse_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_raw_bayer_parse_stop (GstBaseParse * parse);

static gboolean gst_raw_bayer_parse_set_current_config (GstRawBaseParse *
    raw_base_parse, GstRawBaseParseConfig config);
static GstRawBaseParseConfig
gst_raw_bayer_parse_get_current_config (GstRawBaseParse * raw_base_parse);
static gboolean gst_raw_bayer_parse_set_config_from_caps (GstRawBaseParse *
    raw_base_parse, GstRawBaseParseConfig config, GstCaps * caps);
static gboolean gst_raw_bayer_parse_get_caps_from_config (GstRawBaseParse *
    raw_base_parse, GstRawBaseParseConfig config, GstCaps ** caps);
static gsize gst_raw_bayer_parse_get_config_frame_size (GstRawBaseParse *
    raw_base_parse, GstRawBaseParseConfig config);
static guint gst_raw_bayer_parse_get_max_frames_per_buffer (GstRawBaseParse *
    raw_base_parse, GstRawBaseParseConfig config);
static gboolean gst_raw_bayer_parse_is_config_ready (GstRawBaseParse *
    raw_base_parse, GstRawBaseParseConfig config);
static gboolean gst_raw_bayer_parse_process (GstRawBaseParse * raw_base_parse,
    GstRawBaseParseConfig config, GstBuffer * in_data, gsize total_num_in_bytes,
    gsize num_valid_in_bytes, GstBuffer ** processed_data);
static gboolean gst_raw_bayer_parse_is_unit_format_supported (GstRawBaseParse *
    raw_base_parse, GstFormat format);
static void gst_raw_bayer_parse_get_units_per_second (GstRawBaseParse *
    raw_base_parse, GstFormat format, GstRawBaseParseConfig config,
    gsize * units_per_sec_n, gsize * units_per_sec_d);

static gint gst_raw_bayer_parse_get_overhead_size (GstRawBaseParse *
    raw_base_parse, GstRawBaseParseConfig config);

static gboolean gst_raw_bayer_parse_is_using_sink_caps (GstRawBayerParse *
    raw_bayer_parse);
static GstRawBayerParseConfig
    * gst_raw_bayer_parse_get_config_ptr (GstRawBayerParse * raw_bayer_parse,
    GstRawBaseParseConfig config);

static gint gst_raw_bayer_parse_get_alignment (GstRawBaseParse * raw_base_parse,
    GstRawBaseParseConfig config);

static void gst_raw_bayer_parse_init_config (GstRawBayerParseConfig * config);

GType
gst_raw_bayer_parse_format_type (void)
{
  static gsize type = 0;
  static const GEnumValue types[] = {
    {GST_RAW_BAYER_PARSE_FORMAT_BGGR, "GST_RAW_BAYER_PARSE_FORMAT_BGGR",
        "bggr"},
    {GST_RAW_BAYER_PARSE_FORMAT_RGGB, "GST_RAW_BAYER_PARSE_FORMAT_RGGB",
        "rggb"},
    {GST_RAW_BAYER_PARSE_FORMAT_GRBG, "GST_RAW_BAYER_PARSE_FORMAT_GRBG",
        "grbg"},
    {GST_RAW_BAYER_PARSE_FORMAT_GBRG, "GST_RAW_BAYER_PARSE_FORMAT_GBRG",
        "gbrg"},
    {0, NULL, NULL},
  };

  if (g_once_init_enter (&type)) {
    GType tmp = g_enum_register_static ("GstRawBayerParseFormat", types);
    g_once_init_leave (&type, tmp);
  }

  return (GType) type;
}

static void
gst_raw_bayer_parse_class_init (GstRawBayerParseClass * klass)
{
  GObjectClass *object_class;
  GstElementClass *element_class;
  GstBaseParseClass *baseparse_class;
  GstRawBaseParseClass *rawbaseparse_class;

  GST_DEBUG_CATEGORY_INIT (raw_bayer_parse_debug, "rawvideoparse", 0,
      "rawvideoparse element");

  object_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  baseparse_class = GST_BASE_PARSE_CLASS (klass);
  rawbaseparse_class = GST_RAW_BASE_PARSE_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&static_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&static_src_template));

  object_class->set_property =
      GST_DEBUG_FUNCPTR (gst_raw_bayer_parse_set_property);
  object_class->get_property =
      GST_DEBUG_FUNCPTR (gst_raw_bayer_parse_get_property);

  baseparse_class->stop = GST_DEBUG_FUNCPTR (gst_raw_bayer_parse_stop);

  rawbaseparse_class->set_current_config =
      GST_DEBUG_FUNCPTR (gst_raw_bayer_parse_set_current_config);
  rawbaseparse_class->get_current_config =
      GST_DEBUG_FUNCPTR (gst_raw_bayer_parse_get_current_config);
  rawbaseparse_class->set_config_from_caps =
      GST_DEBUG_FUNCPTR (gst_raw_bayer_parse_set_config_from_caps);
  rawbaseparse_class->get_caps_from_config =
      GST_DEBUG_FUNCPTR (gst_raw_bayer_parse_get_caps_from_config);
  rawbaseparse_class->get_config_frame_size =
      GST_DEBUG_FUNCPTR (gst_raw_bayer_parse_get_config_frame_size);
  rawbaseparse_class->get_max_frames_per_buffer =
      GST_DEBUG_FUNCPTR (gst_raw_bayer_parse_get_max_frames_per_buffer);
  rawbaseparse_class->is_config_ready =
      GST_DEBUG_FUNCPTR (gst_raw_bayer_parse_is_config_ready);
  rawbaseparse_class->process = GST_DEBUG_FUNCPTR (gst_raw_bayer_parse_process);
  rawbaseparse_class->is_unit_format_supported =
      GST_DEBUG_FUNCPTR (gst_raw_bayer_parse_is_unit_format_supported);
  rawbaseparse_class->get_units_per_second =
      GST_DEBUG_FUNCPTR (gst_raw_bayer_parse_get_units_per_second);
  rawbaseparse_class->get_overhead_size =
      GST_DEBUG_FUNCPTR (gst_raw_bayer_parse_get_overhead_size);
  rawbaseparse_class->get_alignment =
      GST_DEBUG_FUNCPTR (gst_raw_bayer_parse_get_alignment);

  g_object_class_install_property (object_class,
      PROP_WIDTH,
      g_param_spec_int ("width",
          "Width",
          "Width of frames in raw stream",
          0, G_MAXINT, DEFAULT_WIDTH,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
      );
  g_object_class_install_property (object_class,
      PROP_HEIGHT,
      g_param_spec_int ("height",
          "Height",
          "Height of frames in raw stream",
          0, G_MAXINT,
          DEFAULT_HEIGHT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
      );
  g_object_class_install_property (object_class,
      PROP_FORMAT,
      g_param_spec_enum ("format",
          "Bayer format",
          "Format of Bayer frames in raw stream",
          GST_RAW_BAYER_PARSE_FORMAT_TYPE,
          DEFAULT_FORMAT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
      );
  g_object_class_install_property (object_class,
      PROP_FRAMERATE,
      gst_param_spec_fraction ("framerate",
          "Frame rate",
          "Rate of frames in raw stream",
          0, 1, G_MAXINT, 1,
          DEFAULT_FRAMERATE_N, DEFAULT_FRAMERATE_D,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
      );
  g_object_class_install_property (object_class,
      PROP_PIXEL_ASPECT_RATIO,
      gst_param_spec_fraction ("pixel-aspect-ratio",
          "Pixel aspect ratio",
          "Pixel aspect ratio of frames in raw stream",
          1, 100, 100, 1,
          DEFAULT_PIXEL_ASPECT_RATIO_N, DEFAULT_PIXEL_ASPECT_RATIO_D,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
      );
  g_object_class_install_property (object_class,
      PROP_FRAME_SIZE,
      g_param_spec_uint ("frame-size",
          "Frame size",
          "Size of a frame (0 = frames are tightly packed together)",
          0, G_MAXUINT,
          DEFAULT_FRAME_STRIDE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
      );

  gst_element_class_set_static_metadata (element_class,
      "rawbayerparse",
      "Codec/Parser/Video",
      "Converts unformatted data streams into timestamped Bayer video frames",
      "Carlos Rafael Giani <dv@pseudoterminal.org>");
}

static void
gst_raw_bayer_parse_init (GstRawBayerParse * raw_bayer_parse)
{
  gst_raw_bayer_parse_init_config (&(raw_bayer_parse->properties_config));
  gst_raw_bayer_parse_init_config (&(raw_bayer_parse->sink_caps_config));

  /* As required by GstRawBaseParse, ensure that the current configuration
   * is initially set to be the properties config */
  raw_bayer_parse->current_config = &(raw_bayer_parse->properties_config);

  /* Properties config must be valid from the start, so set its ready value
   * to TRUE, and make sure its bpf value is valid. */
  raw_bayer_parse->properties_config.ready = TRUE;
  raw_bayer_parse->properties_config.frame_size = DEFAULT_FRAME_STRIDE;
}

static void
gst_raw_bayer_parse_set_property (GObject * object, guint prop_id,
    GValue const *value, GParamSpec * pspec)
{
  GstBaseParse *base_parse = GST_BASE_PARSE (object);
  GstRawBaseParse *raw_base_parse = GST_RAW_BASE_PARSE (object);
  GstRawBayerParse *raw_bayer_parse = GST_RAW_BAYER_PARSE (object);
  GstRawBayerParseConfig *props_cfg = &(raw_bayer_parse->properties_config);

  /* All properties are handled similarly:
   * - if the new value is the same as the current value, nothing is done
   * - the parser lock is held while the new value is set
   * - if the properties config is the current config, the source caps are
   *   invalidated to ensure that the code in handle_frame pushes a new CAPS
   *   event out
   * - properties that affect the video frame size call the function to update
   *   the info and also call gst_base_parse_set_min_frame_size() to ensure
   *   that the minimum frame size can hold 1 frame (= one sample for each
   *   channel); to ensure that the min frame size includes any extra padding,
   *   it is set to the result of gst_raw_bayer_parse_get_config_frame_size()
   */

  switch (prop_id) {
    case PROP_WIDTH:
    {
      gint new_width = g_value_get_int (value);

      GST_RAW_BASE_PARSE_CONFIG_MUTEX_LOCK (object);

      if (new_width != props_cfg->width) {
        props_cfg->width = new_width;

        if (!gst_raw_bayer_parse_is_using_sink_caps (raw_bayer_parse)) {
          gst_raw_base_parse_invalidate_src_caps (raw_base_parse);
          gst_base_parse_set_min_frame_size (base_parse,
              gst_raw_bayer_parse_get_config_frame_size (raw_base_parse,
                  GST_RAW_BASE_PARSE_CONFIG_PROPERTIES));
        }
      }

      GST_RAW_BASE_PARSE_CONFIG_MUTEX_UNLOCK (object);
      break;
    }

    case PROP_HEIGHT:
    {
      gint new_height = g_value_get_int (value);

      GST_RAW_BASE_PARSE_CONFIG_MUTEX_LOCK (object);

      if (new_height != props_cfg->height) {
        props_cfg->height = new_height;

        if (!gst_raw_bayer_parse_is_using_sink_caps (raw_bayer_parse)) {
          gst_raw_base_parse_invalidate_src_caps (raw_base_parse);
          gst_base_parse_set_min_frame_size (base_parse,
              gst_raw_bayer_parse_get_config_frame_size (raw_base_parse,
                  GST_RAW_BASE_PARSE_CONFIG_PROPERTIES));
        }
      }

      GST_RAW_BASE_PARSE_CONFIG_MUTEX_UNLOCK (object);
      break;
    }

    case PROP_FORMAT:
    {
      GstRawBayerParseFormat new_format = g_value_get_enum (value);

      GST_RAW_BASE_PARSE_CONFIG_MUTEX_LOCK (object);

      if (new_format != props_cfg->format) {
        props_cfg->format = new_format;

        if (!gst_raw_bayer_parse_is_using_sink_caps (raw_bayer_parse)) {
          gst_raw_base_parse_invalidate_src_caps (raw_base_parse);
          gst_base_parse_set_min_frame_size (base_parse,
              gst_raw_bayer_parse_get_config_frame_size (raw_base_parse,
                  GST_RAW_BASE_PARSE_CONFIG_PROPERTIES));
        }
      }

      GST_RAW_BASE_PARSE_CONFIG_MUTEX_UNLOCK (object);
      break;
    }

    case PROP_PIXEL_ASPECT_RATIO:
    {
      GST_RAW_BASE_PARSE_CONFIG_MUTEX_LOCK (object);

      props_cfg->pixel_aspect_ratio_n =
          gst_value_get_fraction_numerator (value);
      props_cfg->pixel_aspect_ratio_d =
          gst_value_get_fraction_denominator (value);
      GST_DEBUG_OBJECT (raw_bayer_parse, "setting pixel aspect ratio to %u/%u",
          props_cfg->pixel_aspect_ratio_n, props_cfg->pixel_aspect_ratio_d);

      GST_RAW_BASE_PARSE_CONFIG_MUTEX_UNLOCK (object);
      break;
    }

    case PROP_FRAMERATE:
    {
      GST_RAW_BASE_PARSE_CONFIG_MUTEX_LOCK (object);

      props_cfg->framerate_n = gst_value_get_fraction_numerator (value);
      props_cfg->framerate_d = gst_value_get_fraction_denominator (value);
      GST_DEBUG_OBJECT (raw_bayer_parse, "setting framerate to %u/%u",
          props_cfg->framerate_n, props_cfg->framerate_d);

      GST_RAW_BASE_PARSE_CONFIG_MUTEX_UNLOCK (object);
      break;
    }

    case PROP_FRAME_SIZE:
    {
      GST_RAW_BASE_PARSE_CONFIG_MUTEX_LOCK (object);

      props_cfg->frame_size = g_value_get_uint (value);
      if (!gst_raw_bayer_parse_is_using_sink_caps (raw_bayer_parse))
        gst_base_parse_set_min_frame_size (base_parse,
            gst_raw_bayer_parse_get_config_frame_size (raw_base_parse,
                GST_RAW_BASE_PARSE_CONFIG_PROPERTIES));
      GST_RAW_BASE_PARSE_CONFIG_MUTEX_UNLOCK (object);

      break;
    }

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_raw_bayer_parse_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRawBayerParse *raw_bayer_parse = GST_RAW_BAYER_PARSE (object);
  GstRawBayerParseConfig *props_cfg = &(raw_bayer_parse->properties_config);

  switch (prop_id) {
    case PROP_WIDTH:
      GST_RAW_BASE_PARSE_CONFIG_MUTEX_LOCK (object);
      g_value_set_int (value, props_cfg->width);
      GST_RAW_BASE_PARSE_CONFIG_MUTEX_UNLOCK (object);
      break;

    case PROP_HEIGHT:
      GST_RAW_BASE_PARSE_CONFIG_MUTEX_LOCK (object);
      g_value_set_int (value, props_cfg->height);
      GST_RAW_BASE_PARSE_CONFIG_MUTEX_UNLOCK (object);
      break;

    case PROP_FORMAT:
      GST_RAW_BASE_PARSE_CONFIG_MUTEX_LOCK (object);
      g_value_set_enum (value, props_cfg->format);
      GST_RAW_BASE_PARSE_CONFIG_MUTEX_UNLOCK (object);
      break;

    case PROP_PIXEL_ASPECT_RATIO:
      GST_RAW_BASE_PARSE_CONFIG_MUTEX_LOCK (object);
      gst_value_set_fraction (value, props_cfg->pixel_aspect_ratio_n,
          props_cfg->pixel_aspect_ratio_d);
      GST_RAW_BASE_PARSE_CONFIG_MUTEX_UNLOCK (object);

      break;

    case PROP_FRAMERATE:
      GST_RAW_BASE_PARSE_CONFIG_MUTEX_LOCK (object);
      gst_value_set_fraction (value, props_cfg->framerate_n,
          props_cfg->framerate_d);
      GST_RAW_BASE_PARSE_CONFIG_MUTEX_UNLOCK (object);
      break;

    case PROP_FRAME_SIZE:
      GST_RAW_BASE_PARSE_CONFIG_MUTEX_LOCK (object);
      g_value_set_uint (value, raw_bayer_parse->properties_config.frame_size);
      GST_RAW_BASE_PARSE_CONFIG_MUTEX_UNLOCK (object);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_raw_bayer_parse_stop (GstBaseParse * parse)
{
  GstRawBayerParse *raw_bayer_parse = GST_RAW_BAYER_PARSE (parse);

  /* Sink caps config is not ready until caps come in.
   * We are stopping processing, the element is being reset,
   * so the config has to be un-readied.
   * (Since the properties config is not depending on caps,
   * its ready status is always TRUE.) */
  raw_bayer_parse->sink_caps_config.ready = FALSE;

  return GST_BASE_PARSE_CLASS (parent_class)->stop (parse);
}

static gboolean
gst_raw_bayer_parse_set_current_config (GstRawBaseParse * raw_base_parse,
    GstRawBaseParseConfig config)
{
  GstRawBayerParse *raw_bayer_parse = GST_RAW_BAYER_PARSE (raw_base_parse);

  switch (config) {
    case GST_RAW_BASE_PARSE_CONFIG_PROPERTIES:
      raw_bayer_parse->current_config = &(raw_bayer_parse->properties_config);
      break;

    case GST_RAW_BASE_PARSE_CONFIG_SINKCAPS:
      raw_bayer_parse->current_config = &(raw_bayer_parse->sink_caps_config);
      break;

    default:
      g_assert_not_reached ();
  }

  return TRUE;
}

static GstRawBaseParseConfig
gst_raw_bayer_parse_get_current_config (GstRawBaseParse * raw_base_parse)
{
  GstRawBayerParse *raw_bayer_parse = GST_RAW_BAYER_PARSE (raw_base_parse);
  return gst_raw_bayer_parse_is_using_sink_caps (raw_bayer_parse) ?
      GST_RAW_BASE_PARSE_CONFIG_SINKCAPS : GST_RAW_BASE_PARSE_CONFIG_PROPERTIES;
}

static gboolean
gst_raw_bayer_parse_set_config_from_caps (GstRawBaseParse * raw_base_parse,
    GstRawBaseParseConfig config, GstCaps * caps)
{
  GstStructure *structure;
  GstRawBayerParse *raw_bayer_parse = GST_RAW_BAYER_PARSE (raw_base_parse);
  GstRawBayerParseConfig *config_ptr =
      gst_raw_bayer_parse_get_config_ptr (raw_bayer_parse, config);

  g_assert (caps != NULL);

  /* Caps might get copied, and the copy needs to be unref'd.
   * Also, the caller retains ownership over the original caps.
   * So, to make this mechanism also work with cases where the
   * caps are *not* copied, ref the original caps here first. */
  gst_caps_ref (caps);

  structure = gst_caps_get_structure (caps, 0);

  /* For unaligned raw data, the output caps stay the same,
   * except that video/x-unaligned-raw becomes video/x-raw,
   * since the parser aligns the frame data */
  if (gst_structure_has_name (structure, "video/x-unaligned-raw")) {
    /* Copy the caps to be able to modify them */
    GstCaps *new_caps = gst_caps_copy (caps);
    gst_caps_unref (caps);
    caps = new_caps;

    /* Change the media type to video/x-raw , otherwise
     * gst_video_info_from_caps() won't work */
    structure = gst_caps_get_structure (caps, 0);
    gst_structure_set_name (structure, "video/x-raw");
  }

  config_ptr->ready = TRUE;
  if (!gst_structure_get_int (structure, "width", &config_ptr->width))
    config_ptr->ready = FALSE;
  if (!gst_structure_get_int (structure, "height", &config_ptr->height))
    config_ptr->ready = FALSE;
  if (!gst_structure_get_fraction (structure, "pixel-aspect-ratio",
          &config_ptr->pixel_aspect_ratio_n,
          &config_ptr->pixel_aspect_ratio_d)) {
    config_ptr->pixel_aspect_ratio_n = 1;
    config_ptr->pixel_aspect_ratio_d = 1;
  }
  if (!gst_structure_get_fraction (structure, "framerate",
          &config_ptr->framerate_n, &config_ptr->framerate_d)) {
    config_ptr->framerate_n = 0;
    config_ptr->framerate_d = 1;
  }
  config_ptr->frame_size = 0;

  gst_caps_unref (caps);

  return config_ptr->ready;
}

static const char *
gst_raw_bayer_parse_get_format_string (GstRawBayerParseFormat format)
{
  switch (format) {
    case GST_RAW_BAYER_PARSE_FORMAT_BGGR:
      return "bggr";
    case GST_RAW_BAYER_PARSE_FORMAT_RGGB:
      return "rggb";
    case GST_RAW_BAYER_PARSE_FORMAT_GRBG:
      return "grbg";
    case GST_RAW_BAYER_PARSE_FORMAT_GBRG:
      return "gbrg";
    default:
      g_assert_not_reached ();
      return "invalid";
  }
}

static gboolean
gst_raw_bayer_parse_get_caps_from_config (GstRawBaseParse * raw_base_parse,
    GstRawBaseParseConfig config, GstCaps ** caps)
{
  GstRawBayerParse *raw_bayer_parse = GST_RAW_BAYER_PARSE (raw_base_parse);
  GstRawBayerParseConfig *config_ptr =
      gst_raw_bayer_parse_get_config_ptr (raw_bayer_parse, config);

  g_assert (caps != NULL);

  *caps =
      gst_caps_new_simple ("video/x-bayer", "width", G_TYPE_INT,
      config_ptr->width, "height", G_TYPE_INT, config_ptr->height, "format",
      G_TYPE_STRING, gst_raw_bayer_parse_get_format_string (config_ptr->format),
      "framerate", GST_TYPE_FRACTION, config_ptr->framerate_n,
      config_ptr->framerate_d, "pixel-aspect-ratio", GST_TYPE_FRACTION,
      config_ptr->pixel_aspect_ratio_n, config_ptr->pixel_aspect_ratio_d, NULL);

  return *caps != NULL;
}

static gsize
gst_raw_bayer_parse_get_config_frame_size (GstRawBaseParse * raw_base_parse,
    GstRawBaseParseConfig config)
{
  GstRawBayerParse *raw_bayer_parse = GST_RAW_BAYER_PARSE (raw_base_parse);
  GstRawBayerParseConfig *config_ptr =
      gst_raw_bayer_parse_get_config_ptr (raw_bayer_parse, config);
  gsize size = GST_ROUND_UP_4 (config_ptr->width) * config_ptr->height;
  return MAX (size, (gsize) (config_ptr->frame_size));
}

static guint
gst_raw_bayer_parse_get_max_frames_per_buffer (G_GNUC_UNUSED GstRawBaseParse *
    raw_base_parse, G_GNUC_UNUSED GstRawBaseParseConfig config)
{
  /* We want exactly one frame per buffer */
  return 1;
}

static gboolean
gst_raw_bayer_parse_is_config_ready (GstRawBaseParse * raw_base_parse,
    GstRawBaseParseConfig config)
{
  GstRawBayerParse *raw_bayer_parse = GST_RAW_BAYER_PARSE (raw_base_parse);
  return gst_raw_bayer_parse_get_config_ptr (raw_bayer_parse, config)->ready;
}

static gint
gst_raw_bayer_parse_get_alignment (GstRawBaseParse * raw_base_parse,
    GstRawBaseParseConfig config)
{
  return 32;
}

static gboolean
gst_raw_bayer_parse_process (GstRawBaseParse * raw_base_parse,
    GstRawBaseParseConfig config, GstBuffer * in_data,
    G_GNUC_UNUSED gsize total_num_in_bytes,
    G_GNUC_UNUSED gsize num_valid_in_bytes, GstBuffer ** processed_data)
{
  GstRawBayerParse *raw_bayer_parse = GST_RAW_BAYER_PARSE (raw_base_parse);
  GstRawBayerParseConfig *config_ptr =
      gst_raw_bayer_parse_get_config_ptr (raw_bayer_parse, config);
  GstBuffer *out_data;
  gsize size;

  /* In case of extra padding bytes, get a subbuffer without the padding bytes. */
  size = GST_ROUND_UP_4 (config_ptr->width) * config_ptr->height;
  if (size < config_ptr->frame_size) {
    *processed_data = out_data =
        gst_buffer_copy_region (in_data,
        GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS |
        GST_BUFFER_COPY_MEMORY, 0, size);
  } else {
    out_data = in_data;
    *processed_data = NULL;
  }

  return TRUE;
}

static gboolean
gst_raw_bayer_parse_is_unit_format_supported (G_GNUC_UNUSED GstRawBaseParse *
    raw_base_parse, GstFormat format)
{
  switch (format) {
    case GST_FORMAT_BYTES:
    case GST_FORMAT_DEFAULT:
      return TRUE;
    default:
      return FALSE;
  }
}

static void
gst_raw_bayer_parse_get_units_per_second (GstRawBaseParse * raw_base_parse,
    GstFormat format, GstRawBaseParseConfig config, gsize * units_per_sec_n,
    gsize * units_per_sec_d)
{
  GstRawBayerParse *raw_bayer_parse = GST_RAW_BAYER_PARSE (raw_base_parse);
  GstRawBayerParseConfig *config_ptr =
      gst_raw_bayer_parse_get_config_ptr (raw_bayer_parse, config);

  switch (format) {
    case GST_FORMAT_BYTES:
    {
      gsize framesize = GST_ROUND_UP_4 (config_ptr->width) * config_ptr->height;
      gint64 n = framesize * config_ptr->framerate_n;
      gint64 d = config_ptr->framerate_d;
      gint64 common_div = gst_util_greatest_common_divisor_int64 (n, d);
      GST_DEBUG_OBJECT (raw_bayer_parse,
          "n: %" G_GINT64_FORMAT " d: %" G_GINT64_FORMAT " common divisor: %"
          G_GINT64_FORMAT, n, d, common_div);

      /* Divide numerator and denominator by greatest common divisor.
       * This minimizes the risk of integer overflows in the baseparse class. */
      *units_per_sec_n = n / common_div;
      *units_per_sec_d = d / common_div;

      break;
    }

    case GST_FORMAT_DEFAULT:
    {
      *units_per_sec_n = config_ptr->framerate_n;
      *units_per_sec_d = config_ptr->framerate_d;
      break;
    }

    default:
      g_assert_not_reached ();
  }
}

static gint
gst_raw_bayer_parse_get_overhead_size (GstRawBaseParse * raw_base_parse,
    GstRawBaseParseConfig config)
{
  GstRawBayerParse *raw_bayer_parse = GST_RAW_BAYER_PARSE (raw_base_parse);
  GstRawBayerParseConfig *config_ptr =
      gst_raw_bayer_parse_get_config_ptr (raw_bayer_parse, config);
  gint64 info_size = GST_ROUND_UP_4 (config_ptr->width) * config_ptr->height;
  gint64 frame_size = config_ptr->frame_size;

  /* In the video parser, the overhead is defined by the difference between
   * the configured frame size and the raw Bayer frame size. If the former is
   * larger, then the additional bytes are considered padding bytes and get
   * ignored by the base class. */

  GST_LOG_OBJECT (raw_bayer_parse,
      "info size: %" G_GINT64_FORMAT "  frame size: %" G_GINT64_FORMAT,
      info_size, frame_size);

  return (info_size < frame_size) ? (gint) (frame_size - info_size) : 0;
}

static gboolean
gst_raw_bayer_parse_is_using_sink_caps (GstRawBayerParse * raw_bayer_parse)
{
  return raw_bayer_parse->current_config ==
      &(raw_bayer_parse->sink_caps_config);
}

static GstRawBayerParseConfig *
gst_raw_bayer_parse_get_config_ptr (GstRawBayerParse * raw_bayer_parse,
    GstRawBaseParseConfig config)
{
  g_assert (raw_bayer_parse->current_config != NULL);

  switch (config) {
    case GST_RAW_BASE_PARSE_CONFIG_PROPERTIES:
      return &(raw_bayer_parse->properties_config);

    case GST_RAW_BASE_PARSE_CONFIG_SINKCAPS:
      return &(raw_bayer_parse->sink_caps_config);

    default:
      g_assert (raw_bayer_parse->current_config != NULL);
      return raw_bayer_parse->current_config;
  }
}

static void
gst_raw_bayer_parse_init_config (GstRawBayerParseConfig * config)
{
  config->ready = FALSE;
  config->width = DEFAULT_WIDTH;
  config->height = DEFAULT_HEIGHT;
  config->format = DEFAULT_FORMAT;
  config->pixel_aspect_ratio_n = DEFAULT_PIXEL_ASPECT_RATIO_N;
  config->pixel_aspect_ratio_d = DEFAULT_PIXEL_ASPECT_RATIO_D;
  config->framerate_n = DEFAULT_FRAMERATE_N;
  config->framerate_d = DEFAULT_FRAMERATE_D;

  config->frame_size = DEFAULT_FRAME_STRIDE;
}
