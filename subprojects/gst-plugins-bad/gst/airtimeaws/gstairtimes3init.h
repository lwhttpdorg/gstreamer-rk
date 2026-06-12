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

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus

#include <memory>
#include <type_traits>

#include "gstairtimescopedresource.hpp"

extern "C" {
#endif

/**
    Visibility macros, taken from https://gcc.gnu.org/wiki/Visibility
 */

// clang-format off
#if defined _WIN32 || defined __CYGWIN__
  #ifdef BUILDING_DLL
    #ifdef __GNUC__
      #define GST_AIRTIME_DLL_PUBLIC __attribute__ ((dllexport))
    #else
      #define GST_AIRTIME_DLL_PUBLIC __declspec(dllexport) // Note: actually gcc seems to also supports this syntax.
    #endif
  #else
    #ifdef __GNUC__
      #define GST_AIRTIME_DLL_PUBLIC __attribute__ ((dllimport))
    #else
      #define GST_AIRTIME_DLL_PUBLIC __declspec(dllimport) // Note: actually gcc seems to also supports this syntax.
    #endif
  #endif
#else
  #if __GNUC__ >= 4
    #define GST_AIRTIME_DLL_PUBLIC __attribute__ ((visibility ("default")))
  #else
    #define GST_AIRTIME_DLL_PUBLIC
  #endif
#endif
// clang-format on

/**
 * Opaque type.
 */
typedef struct gst_airtime_s3_context gst_airtime_s3_context;

/**
 * @brief The configuration structure for the GstAirtimeContext.
 */
typedef struct GstAirtimeS3ContextConfig {
    /**
     * The size in bytes of the chunks of the S3 object to download is divided into. Must be multiple of the
     * cache_file_chunk_size" config value. 1MB is a good default, but can be adjusted based on the expected size of the
     * S3 objects.
     */
    uint64_t download_chunk_size;

    /**
     * The size in bytes of the cached file chunk. Each downloaded S3 chunk is split into these smaller chunks for
     * storage. The download_chunk_size config value must be multiple of this value.
     * 2MB is a good value, but can be adjusted based on the expected size of the S3 objects.
     */
    uint64_t file_chunk_size;

    /**
     * The maximum number of concurrent S3 chunk downloads to cache the S3 object locally.
     * 25 is a good value, but can be adjusted based on the expected size of the S3 objects and the available network
     * bandwidth.
     */
    uint64_t max_concurrent_downloads;

    /**
     * The base directory to use for local S3 file caching. It points to a local directory where the S3 file is
     * downloaded and stored in chunks. If not set, an OS-specific temporary directory is used as the base cache
     * directory. Each S3 URI is stored in a dedicated bucket/key-specific subdirectory.
     */
    const char* cache_directory;

    /**
     * The maximum total cache size in bytes. When this limit is reached, the LRU eviction policy removes cache
     * directory of the least recently used S3 file. Setting this value to 0 disables eviction making the cache
     * unbounded.
     */
    uint64_t cache_max_size;

    /**
     * The maximum number of retries for S3 fetch operations that fail due to transient errors (e.g., network issues).
     * 3 is a good value, but can be adjusted based on the expected reliability of the S3 service.
     */
    unsigned fetch_max_retry_count;

    /**
     * Whether to trust the integrity of cached data without revalidating it with S3 metadata object. It may avoid
     * unnecessary S3 requests if the metadata is already cached and allows for working with the cached object without
     * having an active internet connection.
     */
    bool trust_cached_data;

    /**
     * Corresponds to the AWS client configuration httpRequestTimeoutMs property.
     * https://docs.aws.amazon.com/sdk-for-cpp/latest/api/aws-cpp-sdk-core/html/struct_aws_1_1_client_1_1_client_configuration.html#a7f812c185f0363d21fe706ca1117c56b
     */
    long http_request_timeout;

    /**
     * Corresponds to the AWS client configuration requestTimeoutMs property.
     * https://docs.aws.amazon.com/sdk-for-cpp/latest/api/aws-cpp-sdk-core/html/struct_aws_1_1_client_1_1_client_configuration.html#a68c35ac8d14619e4bfc77d848fd89473
     */
    long request_timeout;

    /**
     * Whether to validate the S3 credentials before making requests. If enabled, the context will attempt to validate
     * the provided AWS credentials by making a simple request to AWS STS service. If the credentials are invalid, the
     * context creation will fail.
     */
    bool validate_credentials;

    /**
     * Whether to ensure that the S3 client is configured for the correct region of the S3 bucket. If enabled, the
     * context will attempt to determine the correct region for the S3 bucket and configure the S3 client accordingly.
     * This may involve making an additional request to AWS, which could introduce some latency during the first
     * access.
     */
    bool ensure_correct_region;

    /**
     * Whether to elevate S3 errors to std::cout in addition to logging them with GST_ERROR.
     * This is useful because GST_ERROR logs may not be visible to the user, whereas std::cout output is.
     * Specifically, this helps to provide context for errors reported by GStreamer's discovery mechanism.
     */
    bool elevate_s3_errors_to_cout;

} GstAirtimeS3ContextConfig;

GST_AIRTIME_DLL_PUBLIC GstAirtimeS3ContextConfig gst_airtime_s3_context_get_default_config();

/**
 * Create a new GstAirtimeS3Context instance.
 *
 * @param config The configuration for the context.
 * @param context Pointer to the location where the created context instance will be stored.
 * @return 0 on success, non-zero error code on failure.
 */
GST_AIRTIME_DLL_PUBLIC int gst_airtime_s3_context_create(const GstAirtimeS3ContextConfig* config,
                                                         gst_airtime_s3_context** context);

/**
 * Check if the underlying S3 URI providers were newly created or an existing instance was returned.
 * @param context The context instance to check.
 * @return true if the providers were newly created, false if an existing instance was returned.
 */
GST_AIRTIME_DLL_PUBLIC bool gst_airtime_s3_context_newly_created_providers(const gst_airtime_s3_context* context);

/**
 * Destroy the given GstAirtimeS3Context instance.
 *
 * @param context The context instance to destroy.
 * @return 0 on success, non-zero error code on failure.
 */
GST_AIRTIME_DLL_PUBLIC int gst_airtime_s3_context_destroy(gst_airtime_s3_context* context);

#ifdef __cplusplus
} // extern "C"
#endif

#ifdef __cplusplus

namespace gst::airtime
{

using ScopedS3Context = ResourceReleasedByFunction<gst_airtime_s3_context, gst_airtime_s3_context_destroy>;

/// @brief Create a scoped S3 context.
/// @param config The configuration for the context.
/// @return A scoped S3 context instance.
GST_AIRTIME_DLL_PUBLIC ScopedS3Context createScopedS3Context(const GstAirtimeS3ContextConfig& config);

} // namespace gst::airtime

#endif // __cplusplus
