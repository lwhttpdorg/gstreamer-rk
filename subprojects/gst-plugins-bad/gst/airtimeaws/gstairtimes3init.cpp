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

#include "gstairtimes3init.h"
#include "gstairtimes3uriprovidersfactory.hpp"

#include <cassert>
#include <cinttypes>
#include <cstring>
#include <memory>

#include <gst/gst.h>

namespace
{

bool validateConfig(const GstAirtimeS3ContextConfig& config)
{
    if (config.download_chunk_size == 0)
    {
        GST_ERROR("Download chunk size must be greater than 0");
        return false;
    }
    if (config.file_chunk_size == 0)
    {
        GST_ERROR("File chunk size must be greater than 0");
        return false;
    }
    if (config.download_chunk_size % config.file_chunk_size != 0)
    {
        GST_ERROR("Download chunk size must be a multiple of file chunk size");
        return false;
    }
    // max_concurrent_downloads can be 0, which means we rely on the default value
    // max_cache_size_bytes can be 0, which means no limit
    // fetch_max_retry_count can be 0, which means no retries
    // http_request_timeout_ms can be 0, which means no timeout
    // request_timeout_ms can be 0, which means no timeout
    return true;
}

gst::airtime::S3URIProviderConfig getConfig(const GstAirtimeS3ContextConfig& c_config)
{
    gst::airtime::S3URIProviderConfig config;
    config.download_chunk_size = c_config.download_chunk_size;
    config.file_chunk_size = c_config.file_chunk_size;
    config.max_number_of_downloads = (c_config.max_concurrent_downloads == 0)
                                         ? gst::airtime::default_number_of_concurrent_downloads
                                         : c_config.max_concurrent_downloads;
    if (c_config.cache_directory)
    {
        config.cache_base_directory = std::filesystem::path{c_config.cache_directory};
    }
    config.max_cache_size_bytes = c_config.cache_max_size;
    config.fetch_max_retry_count = c_config.fetch_max_retry_count;
    config.trust_cached_data = c_config.trust_cached_data;
    config.http_request_timeout_ms = c_config.http_request_timeout;
    config.request_timeout_ms = c_config.request_timeout;
    config.validate_credentials = c_config.validate_credentials;
    config.ensure_correct_region = c_config.ensure_correct_region;
    config.elevate_s3_errors_to_cout = c_config.elevate_s3_errors_to_cout;
    return config;
}

} // namespace

struct gst_airtime_s3_context {
    explicit gst_airtime_s3_context(const GstAirtimeS3ContextConfig& config) :
        providers_{gst::airtime::S3URIProvidersFactory::create(getConfig(config))}
    {
    }

    bool newlyCreated() const noexcept
    {
        return providers_.second;
    }

private:
    std::pair<std::shared_ptr<gst::airtime::S3URIProviders>, bool> providers_;
};

GstAirtimeS3ContextConfig gst_airtime_s3_context_get_default_config()
{
    GstAirtimeS3ContextConfig config;
    std::memset(&config, 0, sizeof config);
    const gst::airtime::S3URIProviderConfig default_config{};
    config.download_chunk_size = default_config.download_chunk_size;
    config.file_chunk_size = default_config.file_chunk_size;
    config.max_concurrent_downloads = default_config.max_number_of_downloads;
    config.cache_directory = nullptr;
    config.cache_max_size = default_config.max_cache_size_bytes;
    config.fetch_max_retry_count = default_config.fetch_max_retry_count;
    config.trust_cached_data = default_config.trust_cached_data;
    config.http_request_timeout = default_config.http_request_timeout_ms;
    config.request_timeout = default_config.request_timeout_ms;
    config.validate_credentials = default_config.validate_credentials;
    config.ensure_correct_region = default_config.ensure_correct_region;
    config.elevate_s3_errors_to_cout = default_config.elevate_s3_errors_to_cout;
    return config;
}

int gst_airtime_s3_context_create(const GstAirtimeS3ContextConfig* config, gst_airtime_s3_context** context)
{
    assert(config);
    assert(context);

    try
    {
        if (not validateConfig(*config))
        {
            return 1;
        }
        *context = new gst_airtime_s3_context(*config);
        return 0;
    }
    catch (const std::exception& e)
    {
        GST_ERROR("Failed to create airtime S3 context: %s", e.what());
    }
    return 1;
}

bool gst_airtime_s3_context_newly_created_providers(const gst_airtime_s3_context* context)
{
    assert(context);
    return context->newlyCreated();
}

int gst_airtime_s3_context_destroy(gst_airtime_s3_context* context)
{
    assert(context);

    try
    {
        delete context;
        return 0;
    }
    catch (const std::exception& e)
    {
        GST_ERROR("Failed to destroy airtime S3 context: %s", e.what());
    }
    return 1;
}

namespace gst::airtime
{

ScopedS3Context createScopedS3Context(const GstAirtimeS3ContextConfig& config)
{
    gst_airtime_s3_context* context = nullptr;
    if (gst_airtime_s3_context_create(&config, &context) != 0)
    {
        throw std::runtime_error{"Failed to create airtime S3 context"};
    }
    return ScopedS3Context{context};
}

} // namespace gst::airtime