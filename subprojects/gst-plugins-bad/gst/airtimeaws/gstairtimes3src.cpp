/*
 * airtime aws plugin
 * Copyright (C) 2025 mmhmm, Inc.
 *   @author: Teus Groenewoud <teus@mmhmm.app>
 *   @author: Tomasz Mikolajczyk <tomasz.mikolajczyk@mmhmm.app>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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

/**
 * SECTION:element-airtimes3src
 *
 * @title: airtimes3src
 * @short_description: A src element that provides URI-based, seekable, cached access to AWS S3 locations.
 * Caching is performed in an async manner by a pool of threads, allowing for efficient access to large files. Caching
 * is designed to survive between pipeline runs, or to be used by multiple pipelines running inside the same process.
 *
 * The airtimes3src element has caching abilities that can be configured via properties. Caching happens in the form of
 * local file chunks, which are downloaded from S3 and stored in a local cache directory. Common eviction policies are
 * in place to manage the cache size and remove old or unused chunks. Partially cached files can be used to resume S3
 * downloads. Incomplete downloads can be resumed, and the element will attempt to restore the download state from the
 * cache.
 *
 * Buffer filling allows for efficient access to large files. This means that the pipeline is allowed to start
 * processing data even if the entire file is not yet fully downloaded.
 *
 * Download priority is given to the byte ranges requested, which means that seeking is fully supported. The element
 * will prioritize downloading the requested byte ranges first, and then fill in the rest of the file as needed.
 *
 * Note that because this is a download-based source, while real-time playback can be achieved during downloading, it is
 * not guaranteed and fully depends on the network bandwidth and AWS (S3) response times.
 *
 * The element registers itself as a URI handler for S3 URIs, which means that it can be used in GStreamer pipelines,
 * for discovery purposes, in GES projects, etc.
 *
 * The element posts metrics to the bus, which includes the location, content length, download completion state, etc. To
 * listen to these metrics, look for the `airtimes3src::metrics` message.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -v airtimes3src location='s3://bucket/clip.mp4' ! decodebin ! videoconvert ! autovideosink
 *
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstairtimes3src.h"
#include "gstairtimes3srccontext.hpp"

#include "gstairtimes3uriprovidersfactory.hpp"
#include "gstairtimescopedhelpers.hpp"

#include <cassert>
#include <filesystem>
#include <inttypes.h>
#include <mutex>

GST_DEBUG_CATEGORY_STATIC(gst_airtime_s3_src_debug);
#define GST_CAT_DEFAULT gst_airtime_s3_src_debug

static GstStaticPadTemplate caps_factory =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

enum
{
    PROP_0,
    PROP_LOCATION,
    PROP_CACHE_DOWNLOAD_CHUNK_SIZE,
    PROP_CACHE_FILE_CHUNK_SIZE,
    PROP_MAX_NUMBER_OF_DOWNLOADS,
    PROP_CACHE_DIRECTORY,
    PROP_CACHE_MAX_SIZE,
    PROP_FETCH_MAX_RETRY_COUNT,
    PROP_TRUST_CACHED_DATA,
    PROP_HTTP_REQUEST_TIMEOUT,
    PROP_REQUEST_TIMEOUT,
    PROP_VALIDATE_CREDENTIALS,
    PROP_ENSURE_CORRECT_REGION,
    PROP_SOURCE_HINT,
    PROP_FILE_PATTERN
};

#define GST_TYPE_AIRTIME_S3_SRC_SOURCE_HINT (gst_airtime_s3_src_source_hint_get_type())
static GType gst_airtime_s3_src_source_hint_get_type()
{
    static GType source_hint_type{0};

    static const GEnumValue source_hint_values[] = {
        {S3_SOURCE_HINT_TYPE_NONE, "Auto-detect (default)", "none"},
        {S3_SOURCE_HINT_TYPE_KEY, "Single S3 object key", "key"},
        {S3_SOURCE_HINT_TYPE_PREFIX, "S3 directory prefix", "prefix"},
        {0, NULL, NULL},
    };

    if (!source_hint_type)
    {
        source_hint_type = g_enum_register_static("GstAirtimeS3SrcSourceHintType", source_hint_values);
    }
    return source_hint_type;
}

#define S3_URI "s3"
#define S3_PROTOCOL S3_URI "://"

#define AIRTIME_S3_SRC_METRICS_MESSAGE_NAME "airtimes3src::metrics"

struct GstAirtimeS3SrcImpl
{

    std::string location;

    // Configuration for the S3 URI provider. It is configuration of already existing providers or the default
    // configuration if no providers exist yet.
    gst::airtime::S3URIProviderConfig uri_provider_config =
        gst::airtime::S3URIProvidersFactory::create(gst::airtime::S3URIProviderConfig{}).first->getConfig();

    std::string s3_bucket;
    std::string s3_key;

    std::uint64_t content_length{0};
    bool seekable{false};

    gst::airtime::ScopedGstAirtimeS3SrcContext context;
    std::shared_ptr<gst::airtime::S3URIProviders> s3_uri_providers;
    std::shared_ptr<gst::airtime::S3URIProvider> s3_uri_provider;

    std::mutex lock;
};

/**
 * GstAirtimeS3Src:
 *
 * The airtimes3src object structure.
 */
struct _GstAirtimeS3Src
{
    GstBaseSrc parent;

    GstAirtimeS3SrcImpl *impl;
};

// Forward declarations
static void gst_airtime_s3_src_uri_handler_init(gpointer g_iface, gpointer iface_data);

#define gst_airtime_s3_src_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE(GstAirtimeS3Src, gst_airtime_s3_src, GST_TYPE_BASE_SRC,
                        G_IMPLEMENT_INTERFACE(GST_TYPE_URI_HANDLER, gst_airtime_s3_src_uri_handler_init));

namespace
{

    /// @brief Maps the GstAirtimeS3SrcSourceHintType enum value to the corresponding gst::airtime::SourceHint enum value.
    /// @param source_hint_enum_value The GstAirtimeS3SrcSourceHintType enum
    /// @return The corresponding gst::airtime::SourceHint enum value.
    /// @throws std::invalid_argument if the input enum value is invalid.
    gst::airtime::SourceHint mapSourceHintEnum(GstAirtimeS3SrcSourceHintType source_hint_enum_value)
    {
        switch (source_hint_enum_value)
        {
        case S3_SOURCE_HINT_TYPE_NONE:
            return gst::airtime::SourceHint::none;
        case S3_SOURCE_HINT_TYPE_KEY:
            return gst::airtime::SourceHint::key;
        case S3_SOURCE_HINT_TYPE_PREFIX:
            return gst::airtime::SourceHint::prefix;
        default:
            throw std::invalid_argument("Invalid source hint enum value: " +
                                        std::to_string(static_cast<int>(source_hint_enum_value)));
        }
    }

    /// @brief Maps the gst::airtime::SourceHint enum value to the corresponding GstAirtimeS3SrcSourceHintType enum value.
    /// @param source_hint The gst::airtime::SourceHint enum value
    /// @return The corresponding GstAirtimeS3SrcSourceHintType enum value.
    /// @throws std::invalid_argument if the input enum value is invalid.
    GstAirtimeS3SrcSourceHintType mapSourceHintToEnum(gst::airtime::SourceHint source_hint)
    {
        switch (source_hint)
        {
        case gst::airtime::SourceHint::none:
            return S3_SOURCE_HINT_TYPE_NONE;
        case gst::airtime::SourceHint::key:
            return S3_SOURCE_HINT_TYPE_KEY;
        case gst::airtime::SourceHint::prefix:
            return S3_SOURCE_HINT_TYPE_PREFIX;
        default:
            throw std::invalid_argument("Invalid source hint value: " + std::to_string(static_cast<int>(source_hint)));
        }
    }

} // namespace

/// @brief Posts the metrics of the S3 URI provider to the bus. This includes the location, content length, download
/// completion state and duration.
/// @param self The GstAirtimeS3Src instance
static void gst_airtime_s3_src_post_metrics_to_bus(GstAirtimeS3Src *src)
{
    assert(src);
    GstAirtimeS3SrcImpl *impl = src->impl;
    if (not impl)
    {
        // already destroyed - this can happen during discovery use of element.
        return;
    }

    if (not impl->s3_uri_provider)
    {
        GST_WARNING_OBJECT(src, "S3 URI provider is not set, cannot post metrics to bus.");
        return;
    }

    gst::airtime::ScopedGstStructure metrics_structure{gst_structure_new_empty(AIRTIME_S3_SRC_METRICS_MESSAGE_NAME)};

    gst_structure_set(metrics_structure.get(), "location", G_TYPE_STRING, impl->location.c_str(), nullptr);

    try
    {
        gst_structure_set(metrics_structure.get(), "content-length", G_TYPE_UINT64,
                          impl->s3_uri_provider->getContentLength(), nullptr);
        gst_structure_set(metrics_structure.get(), "download-completion-state", G_TYPE_STRING,
                          impl->s3_uri_provider->getDownloadCompletionStateString().data(), nullptr);

        gst_structure_set(metrics_structure.get(), "duration", G_TYPE_UINT64, impl->s3_uri_provider->getMetrics(),
                          nullptr);
    }
    catch (const std::exception &e)
    {
        GST_ERROR_OBJECT(src, "Failed to prepare metrics structure for bus: %s", e.what());
        return;
    }

    gst::airtime::ScopedGstMessage metrics_message{
        gst_message_new_element(GST_OBJECT_CAST(src), metrics_structure.release())};
    auto seq_num = gst_util_seqnum_next(); // get a unique sequence number for the message
    gst_message_set_seqnum(metrics_message.get(), seq_num);

    gst_element_post_message(GST_ELEMENT_CAST(src), metrics_message.release());
}

/// @brief Validate the properties of the GstAirtimeS3Src element. Sets GST element errors where needed.
/// @param src The GstBaseSrc instance (context)
/// @return A boolean indicating success
static gboolean gst_airtime_s3_src_validate_properties(GstBaseSrc *src)
{
    assert(src);

    GstAirtimeS3Src *self = GST_AIRTIMES3SRC(src);
    GstAirtimeS3SrcImpl *impl = self->impl;

    if (impl->location.empty())
    {
        GST_ELEMENT_ERROR(src, RESOURCE, NOT_FOUND, ("No location specified for reading."), (NULL));
        return false;
    }
    if (!gst_uri_is_valid(impl->location.c_str()))
    {
        GST_ELEMENT_ERROR(src, RESOURCE, NOT_FOUND, ("Invalid URI: %s", impl->location.c_str()), (NULL));
        return false;
    }
    if (impl->uri_provider_config.download_chunk_size % impl->uri_provider_config.file_chunk_size != 0)
    {
        // the GST properties have min value validation, so we do not need to check for that here

        GST_WARNING_OBJECT(src,
                           "Download chunk size (%" PRIu64 ") is not multiple of the file chunk size (%" PRIu64 ").",
                           impl->uri_provider_config.download_chunk_size, impl->uri_provider_config.file_chunk_size);

        GST_WARNING_OBJECT(src, "Resetting download chunk size and file chunk size to default values.");

        impl->uri_provider_config.download_chunk_size = gst::airtime::default_download_chunk_size_bytes;
        impl->uri_provider_config.file_chunk_size = gst::airtime::default_file_cache_chunk_size_bytes;
    }
    return true;
}

// context related functions ---------------------------

/// @brief Callback function for querying pads. It checks if the pad can peer query the given GstQuery.
/// @param item The GValue containing the GstPad to query
/// @param value The GValue to set the result of the query
/// @param user_data The user data, which is the GstQuery to run
/// @return A boolean indicating whether the query was successful or not
static gboolean pad_query(const GValue *item, GValue *value, gpointer user_data)
{
    GstPad *pad = (GstPad *)g_value_get_object(item);
    GstQuery *query = (GstQuery *)user_data;
    gboolean res;

    res = gst_pad_peer_query(pad, query);

    if (res)
    {
        g_value_set_boolean(value, TRUE);
        return FALSE;
    }

    GST_DEBUG("pad peer query failed");
    return TRUE;
}

/// @brief Helper function. Runs a query on the pads of the element. It iterates over the source or sink pads, depending
/// on the direction, meanwhile looking for the context.
/// @param element The GstElement instance (context)
/// @param query The GstQuery instance to run
/// @param direction The direction of the pads to query (source or sink)
/// @return A boolean indicating whether the context was found
static gboolean run_query(GstElement *element, GstQuery *query, GstPadDirection direction)
{
    GstIterator *it;
    GstIteratorFoldFunction func = pad_query;
    GValue res = G_VALUE_INIT;

    g_value_init(&res, G_TYPE_BOOLEAN);
    g_value_set_boolean(&res, FALSE);

    // ask neighboring elements for the context
    if (direction == GST_PAD_SRC)
    {
        it = gst_element_iterate_src_pads(element);
    }
    else
    {
        it = gst_element_iterate_sink_pads(element);
    }

    while (gst_iterator_fold(it, func, &res, query) == GST_ITERATOR_RESYNC)
    {
        gst_iterator_resync(it);
    }
    gst_iterator_free(it);

    return g_value_get_boolean(&res);
}

/// @brief Attempts to find the context for the GstAirtimeS3Src element. It first queries the downstream pads for the
/// context, and if not found, the upstream pads.
/// @param src The GstAirtimeS3Src instance (context)
/// @note Inspired by `gstcudautils.h`
static void gst_airtime_s3_src_find_context(GstAirtimeS3Src *src)
{
    assert(src);

    GstContext *ctxt;

    // first, use a query to find the context downstream
    gst::airtime::ScopedGstQuery query{gst_query_new_context(AIRTIME_S3_SRC_CONTEXT_TYPE)};
    if (run_query(GST_ELEMENT(src), query.get(), GST_PAD_SRC))
    {
        // context found downstream: parse it from the query
        gst_query_parse_context(query.get(), &ctxt);
        if (ctxt)
        {
            GST_DEBUG_OBJECT(src, "Context '%s' found downstream.", AIRTIME_S3_SRC_CONTEXT_TYPE);

            // call the virtual function to handle the context
            gst_element_set_context(GST_ELEMENT(src), ctxt);
        }
        return;
    }

    // not found, so query upstream for the context
    if (run_query(GST_ELEMENT(src), query.get(), GST_PAD_SINK))
    {
        // context found upstream: parse it from the query
        gst_query_parse_context(query.get(), &ctxt);
        if (ctxt)
        {
            GST_DEBUG_OBJECT(src, "Context '%s' found upstream.", AIRTIME_S3_SRC_CONTEXT_TYPE);

            // call the virtual function to handle the context
            gst_element_set_context(GST_ELEMENT(src), ctxt);
        }
    }
}

/// @brief The virtual method override for the set_context function of the GstElement class. This method is
/// called when the context of the GstAirtimeS3Src element is set. It updates the internal context and notifies the
/// GstAirtimeS3SrcImpl instance of the new context.
/// @param element The GstElement instance (context)
/// @param context The GstContext instance to set
static void gst_airtime_s3_src_set_context(GstElement *element, GstContext *context)
{
    GstAirtimeS3Src *self = GST_AIRTIMES3SRC(element);
    GstAirtimeS3SrcImpl *impl = self->impl;

    {
        std::lock_guard lock{impl->lock};

        // check if we can use this context
        if (not gst_context_has_context_type(context, AIRTIME_S3_SRC_CONTEXT_TYPE))
        {
            GST_DEBUG_OBJECT(element, "Context '%s' not found in element.", AIRTIME_S3_SRC_CONTEXT_TYPE);
            return;
        }
        const GstStructure *s = gst_context_get_structure(context);
        if (not s)
        {
            GST_ERROR_OBJECT(element, "Context '%s' is empty.", AIRTIME_S3_SRC_CONTEXT_TYPE);
            return;
        }

        // get the airtime S3 src context from the structure
        GstAirtimeS3SrcContext *other_airtime_context{nullptr};
        gst_structure_get(s, AIRTIME_S3_SRC_CONTEXT_TYPE, GST_TYPE_AIRTIME_S3_SRC_CONTEXT, &other_airtime_context,
                          NULL);
        if (not other_airtime_context)
        {
            GST_ERROR_OBJECT(element, "Context '%s' is not of type '%s'.", AIRTIME_S3_SRC_CONTEXT_TYPE,
                             gst_structure_get_name(s));
            return;
        }

        // we want to store the context locally, should we need it later
        impl->context = gst::airtime::ScopedGstAirtimeS3SrcContext{other_airtime_context};

        // now we should be able to access the existing cache instance. It should exist because it was created and
        // placed inside the context in gst_airtime_s3_src_initialize_context
        self->impl->s3_uri_providers = gst::airtime::S3URIProvidersFactory::get();
        assert(self->impl->s3_uri_providers);

        GST_DEBUG_OBJECT(element, "Context '%s' set on element.", AIRTIME_S3_SRC_CONTEXT_TYPE);
    }

    GST_ELEMENT_CLASS(parent_class)->set_context(element, context);
}

/// @brief Initialize the context for the GstAirtimeS3Src element. If no context is found, a new one is created and the
/// cache is inserted.
/// @param src The GstBaseSrc instance (context)
/// @return A boolean indicating success
static gboolean gst_airtime_s3_src_initialize_context(GstBaseSrc *src)
{
    assert(src);

    GstAirtimeS3Src *self = GST_AIRTIMES3SRC(src);
    GstAirtimeS3SrcImpl *impl = self->impl;

    // look for the GST airtime s3 src context
    if (impl->context)
    {
        return true;
    }
    // find the context downstream of upstream, if any elements already exist that may have set it
    gst_airtime_s3_src_find_context(self);
    if (impl->context)
    {
        GST_DEBUG_OBJECT(src, "Context '%s' found, reusing existing context.", AIRTIME_S3_SRC_CONTEXT_TYPE);
        return true;
    }

    GST_DEBUG_OBJECT(self, "Context '%s' not found, creating new context...", AIRTIME_S3_SRC_CONTEXT_TYPE);

    GstContext *context = gst_context_new(AIRTIME_S3_SRC_CONTEXT_TYPE, TRUE);
    gst::airtime::ScopedGstAirtimeS3SrcContext airtime_context{
        gst_airtime_s3_src_context_new(impl->uri_provider_config)};
    if (!airtime_context)
    {
        return false;
    }

    GstStructure *s = gst_context_writable_structure(context);
    gst_structure_set(s, AIRTIME_S3_SRC_CONTEXT_TYPE, GST_TYPE_AIRTIME_S3_SRC_CONTEXT, airtime_context.get(), nullptr);

    // because we have overridden the set_context method, we can safely set the context here and it will be handled in
    // gst_airtime_s3_src_set_context
    gst_element_set_context(GST_ELEMENT(src), context);

    gst::airtime::ScopedGstMessage have_context_message{gst_message_new_have_context(GST_OBJECT(src), context)};
    gst_element_post_message(GST_ELEMENT(src), have_context_message.release());

    return true;
}

/// @brief Start the S3 object fetching and caching. If object is already cached it will be reused.
/// @param src The GstBaseSrc instance (context)
/// @return A boolean indicating success
static bool gst_airtime_s3_src_make_s3_uri_provider_available(GstBaseSrc *src)
{
    assert(src);

    GstAirtimeS3Src *self = GST_AIRTIMES3SRC(src);
    GstAirtimeS3SrcImpl *impl = self->impl;

    GST_DEBUG_OBJECT(src, "Start caching S3 object: '%s'", impl->location.c_str());

    try
    {
        impl->s3_uri_provider = impl->s3_uri_providers->getURIProvider(impl->location, impl->s3_bucket, impl->s3_key);
        assert(impl->s3_uri_provider);
    }
    catch (std::exception &e)
    {
        GST_ELEMENT_ERROR(src, RESOURCE, NOT_FOUND,
                          ("Failed to start element for URI '%s': %s", impl->location.c_str(), e.what()), (NULL));
        return false;
    }
    return true;
}

/// @brief Sets the location property of the GstAirtimeS3Src element.
/// @param src The GstAirtimeS3Src instance (context)
/// @param location The location to set
/// @param err An optional error pointer
/// @return A boolean indicating success
static gboolean gst_airtime_s3_src_set_location(GstAirtimeS3Src *src, const gchar *location, GError **err)
{
    assert(src);

    GstAirtimeS3Src *self = GST_AIRTIMES3SRC(src);
    GstAirtimeS3SrcImpl *impl = self->impl;

    GST_DEBUG_OBJECT(self, "in gst_airtime_s3_src_set_location...");

    // get the element state
    GstState state;
    {
        GST_OBJECT_LOCK(src);
        state = GST_STATE(src);
        GST_OBJECT_UNLOCK(src);
    }
    if (state != GST_STATE_READY && state != GST_STATE_NULL)
    {
        g_warning("Changing the 'location' property on airtimes3src when a file is "
                  "open is not supported.");
        if (err)
        {
            g_set_error(err, GST_URI_ERROR, GST_URI_ERROR_BAD_STATE,
                        "Changing the 'location' property on airtimes3src when a file is "
                        "open is not supported.");
        }
        return false;
    }

    impl->location.clear();
    impl->s3_bucket.clear();
    impl->s3_key.clear();

    // leave filename and uri empty if location is NULL
    if (location != NULL)
    {
        impl->location = location;
        GST_INFO("location : %s", impl->location.c_str());
    }
    g_object_notify(G_OBJECT(src), "location");
    /* FIXME 2.0: notify "uri" property once there is one */

    return true;
}

// GstBaseSrc interface ----------------------------------------------

/// @brief The virtual method override for the GstBaseSrc start method
/// @param src The GstBaseSrc instance (context)
/// @return A boolean indicating success
static gboolean gst_airtime_s3_src_start(GstBaseSrc *src)
{
    assert(src);

    GstAirtimeS3Src *self = GST_AIRTIMES3SRC(src);
    GstAirtimeS3SrcImpl *impl = self->impl;

    GST_DEBUG_OBJECT(self, "Start");

    if (not impl)
    {
        GST_ERROR_OBJECT(src, "S3 source implementation is not initialized.");
        return FALSE;
    }

    try
    {
        if (not gst_airtime_s3_src_validate_properties(src))
        {
            return FALSE;
        }

        // initialize the context
        if (not gst_airtime_s3_src_initialize_context(src))
        {
            GST_ERROR_OBJECT(src, "Failed to initialize context.");
            return FALSE;
        }

        GST_INFO_OBJECT(src, "opening '%s'", impl->location.c_str());

        gst::airtime::ScopedGstURI uri{gst_uri_from_string(impl->location.c_str())};
        std::string s3_bucket{gst_uri_get_host(uri.get())};
        std::string s3_key{gst_uri_get_path(uri.get())};
        if (not s3_key.empty() and s3_key.front() == '/')
        {
            s3_key.erase(0, 1);
        }

        if (impl->s3_bucket == s3_bucket && impl->s3_key == s3_key)
        {
            GST_DEBUG_OBJECT(src, "S3 bucket and key have not changed: skipping re-initialization.");

            return TRUE;
        }

        impl->s3_bucket = std::move(s3_bucket);
        impl->s3_key = std::move(s3_key);

        GST_INFO_OBJECT(src, "S3 bucket  : %s", impl->s3_bucket.c_str());
        GST_INFO_OBJECT(src, "S3 key     : %s", impl->s3_key.c_str());

        if (not gst_airtime_s3_src_make_s3_uri_provider_available(src))
        {
            return FALSE;
        }

        impl->content_length = impl->s3_uri_provider->getContentLength();
        impl->seekable = true;
        gst_base_src_set_dynamic_size(src, impl->seekable);
    }
    catch (const std::exception &e)
    {
        GST_ELEMENT_ERROR(src, RESOURCE, NOT_FOUND,
                          ("Failed to start caching S3 object '%s': %s", impl->location.c_str(), e.what()), (NULL));
        return FALSE;
    }
    return TRUE;
}

/// @brief The virtual method override for the GstBaseSrc stop method
/// @param src The GstBaseSrc instance (context)
/// @return A boolean indicating success
static gboolean gst_airtime_s3_src_stop(GstBaseSrc *src)
{
    assert(src);
    GstAirtimeS3Src *self = GST_AIRTIMES3SRC(src);
    GstAirtimeS3SrcImpl *impl = self->impl;
    assert(impl);

    std::lock_guard lock{impl->lock};

    gst_airtime_s3_src_post_metrics_to_bus(self);

    GST_DEBUG_OBJECT(self, "Stop");

    return TRUE;
}

/// @brief The virtual method override for the GstBaseSrc is_seekable method
/// @param src The GstBaseSrc instance (context)
/// @return A boolean indicating if the source is seekable
static gboolean gst_airtime_s3_src_is_seekable(GstBaseSrc *src)
{
    assert(src);

    GstAirtimeS3Src *self = GST_AIRTIMES3SRC(src);
    GstAirtimeS3SrcImpl *impl = self->impl;

    std::lock_guard lock{impl->lock};

    return impl->seekable;
}

/// @brief The virtual method override for the GstBaseSrc get_size method
/// @param src The GstBaseSrc instance (context)
/// @param size The size of the source
/// @return A boolean indicating success
static gboolean gst_airtime_s3_src_get_size(GstBaseSrc *src, guint64 *size)
{
    assert(src);
    assert(size);

    GstAirtimeS3Src *self = GST_AIRTIMES3SRC(src);
    GstAirtimeS3SrcImpl *impl = self->impl;

    std::lock_guard lock{impl->lock};

    *size = impl->content_length;

    return TRUE;
}

/// @brief The virtual method override for the GstBaseSrc fill method
/// @param src The GstBaseSrc instance (context)
/// @param offset The offset to start filling from
/// @param size The size to fill
/// @param buf The buffer to fill
/// @return A GstFlowReturn indicating the result of the operation
static GstFlowReturn gst_airtime_s3_src_fill(GstBaseSrc *src, guint64 offset, guint size, GstBuffer *buf)
{
    assert(src);
    assert(buf);

    GstAirtimeS3Src *self = GST_AIRTIMES3SRC(src);
    GstAirtimeS3SrcImpl *impl = self->impl;

    std::lock_guard lock{impl->lock};

    GST_DEBUG_OBJECT(src, "Reading %d bytes at offset %" PRIu64, size, offset);

    // make sure the offset is within the content length
    if (offset >= impl->content_length || offset + size > impl->content_length)
    {
        // files should EOS if more than available was requested
        GST_DEBUG_OBJECT(self, "End of file reached");
        return GST_FLOW_EOS;
    }

    // map the buffer so we can write to it
    auto write_buf_map = gst::airtime::getMappedWriteBuffer(buf);
    std::pair<gst::airtime::S3BufferFillStatus, std::uint64_t> status_and_bytes_read;
    try
    {
        status_and_bytes_read = self->impl->s3_uri_provider->fill(write_buf_map.data(), offset, size);
    }
    catch (const std::exception &e)
    {
        GST_ELEMENT_ERROR(src, RESOURCE, READ, ("Failed to read from cache: %s", e.what()), (NULL));
        return GST_FLOW_ERROR;
    }
    const auto [status, bytes_read] = status_and_bytes_read;

    if (status == gst::airtime::S3BufferFillStatus::end_of_file)
    {
        GST_DEBUG_OBJECT(self, "End of file reached");
        return GST_FLOW_EOS;
    }

    auto buffer_size = gst_buffer_get_size(buf);
    if (bytes_read != buffer_size)
    {
        gst_buffer_resize(buf, 0, bytes_read);
    }

    GST_BUFFER_OFFSET(buf) = offset;
    GST_BUFFER_OFFSET_END(buf) = offset + size;

    return GST_FLOW_OK;
}

// GstURIHandler interface -----------------------------------------------

/// @brief The virtual method override for the GstURIHandler get_type method
/// @param type The GType of the handler
/// @return The GstURIType of the handler
static GstURIType gst_airtime_s3_src_uri_get_type(GType)
{
    return GST_URI_SRC;
}

/// @brief The virtual method override for the GstURIHandler get_protocols method
/// @param type The GType of the handler
/// @return A list of supported protocols
static const gchar *const *gst_airtime_s3_src_uri_get_protocols(GType)
{
    static const gchar *protocols[] = {S3_URI, NULL};

    return protocols;
}

/// @brief The virtual method override for the GstURIHandler get_uri method
/// @param handler The GstURIHandler instance (context)
/// @return The URI of the handler
static gchar *gst_airtime_s3_src_uri_get_uri(GstURIHandler *handler)
{
    assert(handler);

    GstAirtimeS3Src *self = GST_AIRTIMES3SRC(handler);
    GstAirtimeS3SrcImpl *impl = self->impl;

    std::lock_guard lock{impl->lock};

    return g_strdup(impl->location.c_str());
}

/// @brief The virtual method override for the GstURIHandler set_uri method
/// @param handler The GstURIHandler instance (context)
/// @param uri The URI to set
/// @param err The error to set if the URI is invalid
/// @return A boolean indicating success
static gboolean gst_airtime_s3_src_uri_set_uri(GstURIHandler *handler, const gchar *uri, GError **err)
{
    assert(handler);

    GstAirtimeS3Src *self = GST_AIRTIMES3SRC(handler);
    GstAirtimeS3SrcImpl *impl = self->impl;

    std::lock_guard lock{impl->lock};

    std::string uri_str{uri};

    // first, support test queries performed by gst_element_make_from_uri
    // GST may do this to test if there is an element that supports a particular URI
    if (uri && uri_str == S3_PROTOCOL)
    {
        gst_airtime_s3_src_set_location(self, NULL, NULL);
        return true;
    }

    if (not gst_uri_is_valid(uri_str.c_str()))
    {
        g_set_error(err, GST_URI_ERROR, GST_URI_ERROR_BAD_URI, "invalid URI '%s'", uri_str.c_str());
        return false;
    }

    return gst_airtime_s3_src_set_location(self, uri_str.c_str(), err);
}

static void gst_airtime_s3_src_uri_handler_init(gpointer g_iface, gpointer)
{
    GstURIHandlerInterface *iface = (GstURIHandlerInterface *)g_iface;

    iface->get_type = gst_airtime_s3_src_uri_get_type;
    iface->get_protocols = gst_airtime_s3_src_uri_get_protocols;
    iface->get_uri = gst_airtime_s3_src_uri_get_uri;
    iface->set_uri = gst_airtime_s3_src_uri_set_uri;
}

// GObject interface -----------------------------------------------------

/// @brief The virtual method override for get property
/// @param object The GObject instance
/// @param prop_id The property ID
/// @param value The value to retrieve the property into
/// @param pspec Additional property information
static void gst_airtime_s3_src_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    GstAirtimeS3Src *self = GST_AIRTIMES3SRC(object);
    GstAirtimeS3SrcImpl *impl = self->impl;
    std::lock_guard lock{impl->lock};

    switch (prop_id)
    {
    case PROP_LOCATION:
        g_value_set_string(value, impl->location.c_str());
        break;
    case PROP_CACHE_DOWNLOAD_CHUNK_SIZE:
        g_value_set_uint64(value, impl->uri_provider_config.download_chunk_size);
        break;
    case PROP_CACHE_FILE_CHUNK_SIZE:
        g_value_set_uint64(value, impl->uri_provider_config.file_chunk_size);
        break;
    case PROP_MAX_NUMBER_OF_DOWNLOADS:
        g_value_set_uint64(value, impl->uri_provider_config.max_number_of_downloads);
        break;
    case PROP_CACHE_DIRECTORY:
        g_value_set_string(value, impl->uri_provider_config.cache_base_directory.c_str());
        break;
    case PROP_CACHE_MAX_SIZE:
        g_value_set_uint64(value, impl->uri_provider_config.max_cache_size_bytes);
        break;
    case PROP_FETCH_MAX_RETRY_COUNT:
        g_value_set_uint(value, impl->uri_provider_config.fetch_max_retry_count);
        break;
    case PROP_TRUST_CACHED_DATA:
        g_value_set_boolean(value, impl->uri_provider_config.trust_cached_data);
        break;
    case PROP_HTTP_REQUEST_TIMEOUT:
        g_value_set_long(value, impl->uri_provider_config.http_request_timeout_ms);
        break;
    case PROP_REQUEST_TIMEOUT:
        g_value_set_long(value, impl->uri_provider_config.request_timeout_ms);
        break;
    case PROP_VALIDATE_CREDENTIALS:
        g_value_set_boolean(value, impl->uri_provider_config.validate_credentials);
        break;
    case PROP_ENSURE_CORRECT_REGION:
        g_value_set_boolean(value, impl->uri_provider_config.ensure_correct_region);
        break;
    case PROP_SOURCE_HINT:
        g_value_set_enum(value, mapSourceHintToEnum(impl->uri_provider_config.source_hint));
        break;
    case PROP_FILE_PATTERN:
        g_value_set_string(value, impl->uri_provider_config.file_pattern.c_str());
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

/// @brief The virtual method override for set property
/// @param object The GObject instance
/// @param prop_id The property ID
/// @param value The value to set
/// @param pspec Additional property information
static void gst_airtime_s3_src_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    GstAirtimeS3Src *self = GST_AIRTIMES3SRC(object);
    GstAirtimeS3SrcImpl *impl = self->impl;
    std::lock_guard lock{impl->lock};

    switch (prop_id)
    {
    case PROP_LOCATION:
        gst_airtime_s3_src_set_location(self, g_value_get_string(value), NULL);
        break;
    case PROP_CACHE_DOWNLOAD_CHUNK_SIZE:
        impl->uri_provider_config.download_chunk_size = g_value_get_uint64(value);
        GST_DEBUG_OBJECT(self, "Download chunk size set to %" PRIu64,
                         impl->uri_provider_config.download_chunk_size);
        break;
    case PROP_CACHE_FILE_CHUNK_SIZE:
        impl->uri_provider_config.file_chunk_size = g_value_get_uint64(value);
        GST_DEBUG_OBJECT(self, "File chunk size set to %" PRIu64, impl->uri_provider_config.file_chunk_size);
        break;
    case PROP_MAX_NUMBER_OF_DOWNLOADS:
        impl->uri_provider_config.max_number_of_downloads = g_value_get_uint64(value);
        if (impl->uri_provider_config.max_number_of_downloads == 0)
        {
            impl->uri_provider_config.max_number_of_downloads =
                gst::airtime::default_number_of_concurrent_downloads;
        }
        GST_DEBUG_OBJECT(self, "Max number of downloads set to %" PRIu64,
                         impl->uri_provider_config.max_number_of_downloads);
        break;
    case PROP_CACHE_DIRECTORY:
        impl->uri_provider_config.cache_base_directory = g_value_get_string(value);
        GST_DEBUG_OBJECT(self, "Cache directory set to %s", impl->uri_provider_config.cache_base_directory.c_str());
        break;
    case PROP_CACHE_MAX_SIZE:
        impl->uri_provider_config.max_cache_size_bytes = g_value_get_uint64(value);
        GST_DEBUG_OBJECT(self, "Max cache size set to %" PRIu64, impl->uri_provider_config.max_cache_size_bytes);
        break;
    case PROP_FETCH_MAX_RETRY_COUNT:
        impl->uri_provider_config.fetch_max_retry_count = g_value_get_uint(value);
        GST_DEBUG_OBJECT(self, "Fetch max retry count set to %u", impl->uri_provider_config.fetch_max_retry_count);
        break;
    case PROP_TRUST_CACHED_DATA:
        impl->uri_provider_config.trust_cached_data = g_value_get_boolean(value);
        GST_DEBUG_OBJECT(self, "Trust cached data set to %s",
                         impl->uri_provider_config.trust_cached_data ? "true" : "false");
        break;
    case PROP_HTTP_REQUEST_TIMEOUT:
        impl->uri_provider_config.http_request_timeout_ms = g_value_get_long(value);
        GST_DEBUG_OBJECT(self, "HTTP request timeout set to %ld ms.",
                         impl->uri_provider_config.http_request_timeout_ms);
        break;
    case PROP_REQUEST_TIMEOUT:
        impl->uri_provider_config.request_timeout_ms = g_value_get_long(value);
        GST_DEBUG_OBJECT(self, "Request timeout set to %ld ms.", impl->uri_provider_config.request_timeout_ms);
        break;
    case PROP_VALIDATE_CREDENTIALS:
        impl->uri_provider_config.validate_credentials = g_value_get_boolean(value);
        GST_DEBUG_OBJECT(self, "Validate credentials set to %s",
                         impl->uri_provider_config.validate_credentials ? "true" : "false");
        break;
    case PROP_ENSURE_CORRECT_REGION:
        impl->uri_provider_config.ensure_correct_region = g_value_get_boolean(value);
        GST_DEBUG_OBJECT(self, "Ensure correct region set to %s",
                         impl->uri_provider_config.ensure_correct_region ? "true" : "false");
        break;
    case PROP_SOURCE_HINT:
        impl->uri_provider_config.source_hint =
            mapSourceHintEnum(static_cast<GstAirtimeS3SrcSourceHintType>(g_value_get_enum(value)));
        GST_DEBUG_OBJECT(self, "Source hint set to %d", static_cast<int>(impl->uri_provider_config.source_hint));
        break;
    case PROP_FILE_PATTERN:
        impl->uri_provider_config.file_pattern = g_value_get_string(value);
        GST_DEBUG_OBJECT(self, "File pattern set to '%s'", impl->uri_provider_config.file_pattern.c_str());
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

/// @brief The method called by GStreamer to finalize or destroy the element
/// @param object The GObject instance (context)
static void gst_airtime_s3_src_finalize(GObject *object)
{
    GstAirtimeS3Src *self = GST_AIRTIMES3SRC(object);
    assert(self->impl);

    delete self->impl;
    self->impl = nullptr;

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

/// @brief The method called by GStreamer to initialize the element
/// @param self The GstAirtimeS3Src instance (context)
static void gst_airtime_s3_src_init(GstAirtimeS3Src *self)
{
    GST_DEBUG_OBJECT(self, "Initializing GstAirtimeS3Src...");

    assert(not self->impl);
    try
    {
        self->impl = new GstAirtimeS3SrcImpl();
    }
    catch (const std::exception &e)
    {
        GST_ERROR_OBJECT(self, "Failed to initialize GstAirtimeS3Src: %s", e.what());
    }
}

/// @brief The method called by GStreamer to initialize the class
/// @param klass The GstAirtimeS3SrcClass instance
static void gst_airtime_s3_src_class_init(GstAirtimeS3SrcClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    GstBaseSrcClass *src_class = GST_BASE_SRC_CLASS(klass);

    object_class->finalize = gst_airtime_s3_src_finalize;
    object_class->set_property = gst_airtime_s3_src_set_property;
    object_class->get_property = gst_airtime_s3_src_get_property;
    const gst::airtime::S3URIProviderConfig default_config;

    /**
     * airtimes3src:location:
     *
     * The location to use for the source. This should be a valid AWS S3 uri
     * as follows: 's3://<s3_bucket>/<s3_key>'. AWS authentication is assumed to have been handled by the
     * environment.
     *
     * Since: 1.26
     */
    g_object_class_install_property(
        object_class, PROP_LOCATION,
        g_param_spec_string(
            "location", "Location",
            "The location to use for the source. This should be a valid AWS S3 uri "
            "as follows: 's3://<s3_bucket>/<s3_key>'. AWS authentication is assumed to have been handled by the "
            "environment.",
            "", (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

    /**
     * airtimes3src:download-chunk-size-bytes:
     *
     * The size in bytes of the chunks of the S3 object to download is divided into. Must be multiple of the
     * file-chunk-size property value.
     *
     * Since: 1.26
     */
    g_object_class_install_property(
        object_class, PROP_CACHE_DOWNLOAD_CHUNK_SIZE,
        g_param_spec_uint64(
            "download-chunk-size-bytes", "S3 download chunk size in bytes",
            "The size in bytes of the chunks of the S3 object to download is divided into. Must be multiple of the "
            "file-chunk-size property value.",
            512, G_MAXUINT64, default_config.download_chunk_size,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

    /**
     * airtimes3src:file-chunk-size-bytes:
     *
     * The size in bytes of the cached file chunk. Each downloaded S3 chunk is split into these
     * smaller chunks for storage. The download-chunk-size property value must be multiple of this value.
     *
     * Since: 1.26
     */
    g_object_class_install_property(
        object_class, PROP_CACHE_FILE_CHUNK_SIZE,
        g_param_spec_uint64(
            "file-chunk-size-bytes", "Local file chunk size in bytes",
            "The size in bytes of the cached file chunk. Each downloaded S3 chunk is split into these "
            "smaller chunks for storage. The download-chunk-size property value must be multiple of this value.",
            512, G_MAXUINT64, default_config.file_chunk_size,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

    /**
     * airtimes3src:max-concurrent-downloads:
     *
     * The maximum number of concurrent S3 chunk downloads to cache the S3 object locally.
     *
     * Since: 1.26
     */
    g_object_class_install_property(
        object_class, PROP_MAX_NUMBER_OF_DOWNLOADS,
        g_param_spec_uint64("max-concurrent-downloads", "Max number of concurrent S3 chunk downloads",
                            "The maximum number of concurrent S3 chunk downloads to cache the S3 object locally. If "
                            "set to 0, the default value will be used as defined in the AWS SDK.",
                            0, G_MAXUINT64, default_config.max_number_of_downloads,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

    /**
     * airtimes3src:cache-directory:
     *
     * The base directory to use for local S3 file caching. It points to a local directory where the S3 file is
     * downloaded and stored in chunks. If not set, an OS-specific temporary directory is used as the base cache
     * directory. Each S3 URI is stored in a dedicated bucket/key-specific subdirectory.
     * Since: 1.26
     */
    g_object_class_install_property(
        object_class, PROP_CACHE_DIRECTORY,
        g_param_spec_string(
            "cache-directory", "Cache directory",
            "The base directory to use for local S3 file caching. It points to a local directory where the S3 file is "
            "downloaded and stored in chunks. If not set, an OS-specific temporary directory is used as the base cache "
            "directory. Each S3 URI is stored in a dedicated bucket/key-specific subdirectory.",
            default_config.cache_base_directory.c_str(),
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

    /**
     * airtimes3src:cache-max-size-bytes:
     *
     * The maximum total cache size in bytes. When this limit is reached, the LRU eviction policy removes cache
     * directory of the least recently used S3 file.
     *
     * Since: 1.26
     */
    g_object_class_install_property(
        object_class, PROP_CACHE_MAX_SIZE,
        g_param_spec_uint64("cache-max-size-bytes", "Max cache size in bytes",
                            "The maximum total cache size in bytes. When this limit is reached, the LRU eviction "
                            "policy removes cache directory of the least recently used S3 file. Setting this value to "
                            "0 disables eviction making the cache unbounded.",
                            0, G_MAXUINT64, default_config.max_cache_size_bytes,
                            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

    /**
     * airtimes3src:fetch-max-retry-count:
     *
     * The maximum number of retries for S3 fetch operations that fail due to transient errors (e.g., network issues).
     *
     * Since: 1.26
     */
    g_object_class_install_property(
        object_class, PROP_FETCH_MAX_RETRY_COUNT,
        g_param_spec_uint("fetch-max-retry-count", "Max number of fetch retry count",
                          "The maximum number of retries for S3 fetch operations that fail due to transient errors "
                          "(e.g., network issues).",
                          0, G_MAXUINT, default_config.fetch_max_retry_count,
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

    /**
     * airtimes3src:trust-cached-data:
     *
     * Whether to trust the integrity of cached data without revalidating it with S3 metadata object.
     * It may avoid unnecessary S3 requests if the metadata is already cached and allows for
     * working with the cached object without having an active internet connection.
     *
     * Since: 1.26
     */
    g_object_class_install_property(
        object_class, PROP_TRUST_CACHED_DATA,
        g_param_spec_boolean(
            "trust-cached-data", "Trust cached data",
            "Whether to trust the integrity of cached data without revalidating it with S3 metadata "
            "object. It may avoid unnecessary S3 requests if the metadata is already cached and allows for "
            "working with the cached object without having an active internet connection.",
            default_config.trust_cached_data,
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

    /**
     * airtimes3src:http-request-timeout:
     *
     * Corresponds to the AWS client configuration `httpRequestTimeoutMs` property. See AWS documentation for more
     * details.
     *
     * Since: 1.26
     */
    g_object_class_install_property(
        object_class, PROP_HTTP_REQUEST_TIMEOUT,
        g_param_spec_long("http-request-timeout", "HTTP request timeout",
                          "Corresponds to the AWS client configuration httpRequestTimeoutMs property.", 0, G_MAXLONG,
                          default_config.http_request_timeout_ms,
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

    /**
     * airtimes3src:request-timeout:
     *
     * Corresponds to the AWS client configuration `requestTimeoutMs` property. See AWS documentation for more
     * details.
     *
     * Since: 1.26
     */
    g_object_class_install_property(
        object_class, PROP_REQUEST_TIMEOUT,
        g_param_spec_long("request-timeout", "Request timeout",
                          "Corresponds to the AWS client configuration requestTimeoutMs property.", 0, G_MAXLONG,
                          default_config.request_timeout_ms,
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

    /**
     * airtimes3src:validate-credentials:
     *
     * Whether to validate AWS credentials before using them. If enabled, the element will attempt to
     * validate the provided AWS credentials by making a simple request to AWS STS service. If the
     * credentials are invalid, the element will fail to start.
     * Since: 1.26
     */
    g_object_class_install_property(
        object_class, PROP_VALIDATE_CREDENTIALS,
        g_param_spec_boolean("validate-credentials", "Validate AWS credentials",
                             "Whether to validate AWS credentials before using them. If enabled, the element will "
                             "attempt to validate the provided AWS credentials by making a simple request to AWS STS "
                             "service. If the credentials are invalid, the element will fail to start.",
                             default_config.validate_credentials,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

    /**
     * airtimes3src:ensure-correct-region:
     *
     * Whether to ensure that the S3 bucket is accessed in the correct region. If enabled, the element will
     * attempt to determine the correct region for the specified S3 bucket and configure the S3 client accordingly.
     * Since: 1.26
     */
    g_object_class_install_property(
        object_class, PROP_ENSURE_CORRECT_REGION,
        g_param_spec_boolean("ensure-correct-region", "Ensure correct S3 bucket region",
                             "Whether to ensure that the S3 bucket is accessed in the correct region. If enabled, the "
                             "element will attempt to determine the correct region for the specified S3 bucket and "
                             "configure the S3 client accordingly.",
                             default_config.ensure_correct_region,
                             (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

    /**
     * airtimes3src:source-hint:
     *
     * Hint for whether the S3 URI points to a single object (key) or a directory (prefix).
     * When set to 'none' (default), the element auto-detects by trying HeadObject first, then
     * falling back to ListObjectsV2. Use 'key' to force single-object mode or 'prefix' to force
     * directory mode.
     *
     * Since: 1.30
     */
    g_object_class_install_property(
        object_class, PROP_SOURCE_HINT,
        g_param_spec_enum("source-hint", "Source hint",
                          "Hint for whether the S3 URI points to a single object (key) or a directory (prefix). "
                          "When set to 'none' (default), the element auto-detects by trying HeadObject first, then "
                          "falling back to ListObjectsV2. Use 'key' to force single-object mode or 'prefix' to force "
                          "directory mode.",
                          GST_TYPE_AIRTIME_S3_SRC_SOURCE_HINT, mapSourceHintToEnum(default_config.source_hint),
                          (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

    /**
     * airtimes3src:file-pattern:
     *
     * Glob pattern to filter files when operating in directory/prefix mode (e.g. "*.ts", "segment_*").
     * An empty string (default) means include all files under the prefix.
     *
     * Since: 1.30
     */
    g_object_class_install_property(
        object_class, PROP_FILE_PATTERN,
        g_param_spec_string(
            "file-pattern", "File pattern",
            "Glob pattern to filter files when operating in directory/prefix mode (e.g. '*.ts', 'segment_*'). "
            "An empty string (default) means include all files under the prefix.",
            default_config.file_pattern.c_str(),
            (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

    gst_element_class_set_details_simple(
        element_class, "airtime S3 (file) src element", "Source", "Serves as an S3 (file) source element",
        "Teus Groenewoud <teus@hotmail.com>, Tomasz Mikolajczyk <tmmikolajczyk@gmail.com>");

    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&caps_factory));

    element_class->set_context = GST_DEBUG_FUNCPTR(gst_airtime_s3_src_set_context);

    src_class->start = GST_DEBUG_FUNCPTR(gst_airtime_s3_src_start);
    src_class->stop = GST_DEBUG_FUNCPTR(gst_airtime_s3_src_stop);
    src_class->is_seekable = GST_DEBUG_FUNCPTR(gst_airtime_s3_src_is_seekable);
    src_class->get_size = GST_DEBUG_FUNCPTR(gst_airtime_s3_src_get_size);
    src_class->fill = GST_DEBUG_FUNCPTR(gst_airtime_s3_src_fill);

    GST_DEBUG_CATEGORY_INIT(gst_airtime_s3_src_debug, "airtimes3src", 0, "airtime S3 src element");
}
