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

#include "gstairtimes3uriproviders.hpp"

#include <cassert>
#include <iostream>

#include <gst/gst.h>

namespace gst::airtime
{

S3URIProviders::S3URIProviders(S3URIProviderConfig config) :
    config_{config},
    cache_manager_{std::make_shared<S3URICacheManager>(config.cache_base_directory, createEvictionPolicy(config_))}
{
}

std::shared_ptr<S3URIProvider> S3URIProviders::getURIProvider(std::string uri, std::string_view s3_bucket,
                                                              std::string_view s3_key)
{
    std::lock_guard lock{uri_providers_access_};

    auto [it, inserted] = uri_providers_.try_emplace(std::move(uri));
    try
    {
        std::shared_ptr<S3URIProvider> provider;
        if (not inserted)
        {
            // key already exists, trying to obtain existing/already in use shared_ptr provider from weak_ptr
            provider = it->second.lock();
            if (not provider)
            {
                // no actively used provider - creating a new one
                provider = createProviderWithRetry(s3_bucket, s3_key);
                it->second = provider;
            }
            else
            {
                // provider has been already created before. Checking if it is in a healhthy state. If not, we will
                // create a new one.
                if (provider.use_count() == 1 and provider->getErrorType().has_value())
                {
                    GST_ERROR("Provider for URI %s has an error: %s", uri.c_str(),
                              provider->getLastErrorMessage().value_or("Unknown error").c_str());
                    provider = createProviderWithRetry(s3_bucket, s3_key);
                    it->second = provider;
                }
            }
        }
        else
        {
            // a new provider to be created
            provider = createProviderWithRetry(s3_bucket, s3_key);
            it->second = provider;
        }
        assert(provider);
        return provider;
    }
    catch (const std::exception& e)
    {
        uri_providers_.erase(it);
        throw;
    }
}

std::shared_ptr<S3URIProvider> S3URIProviders::createProviderWithRetry(std::string_view s3_bucket,
                                                                       std::string_view s3_key)
{
    retry_count_ = 0;
    return createProvider(s3_bucket, s3_key);
}

std::shared_ptr<S3URIProvider> S3URIProviders::createProvider(std::string_view s3_bucket, std::string_view s3_key)
{
    try
    {
        return createS3URIProvider(cache_manager_, s3_bucket, s3_key, config_);
    }
    catch (const std::exception& e)
    {
        const auto error_message = std::format("Failed to create S3URIProvider for bucket: {}, key: {}. Error: {}",
                                               s3_bucket, s3_key, e.what());
        GST_ERROR("%s", error_message.c_str());

        if (config_.elevate_s3_errors_to_cout)
        {
            // We elevate to the console, as the GST_ERROR log may not be visible to the user. We do this to add context
            // to the following error message posted directly to the console by GST discovery:
            //
            // "Discovery error, domain: 2761,code: 4, message: Stream doesn't contain enough data."
            //
            // By posting to the console ourselves, we can at least give the user a hint that the error is likely due to
            // inability to access the S3 object. This results in the following:
            //
            // "Failed to create S3URIProvider for bucket: [bucket], key: [key]. Error: Error retrieving S3 object
            // metadata: The requested AWS S3 resource was not found"
            //
            std::cout << error_message << std::endl;
        }
        if (++retry_count_ > config_.fetch_max_retry_count)
        {
            GST_ERROR("Failed to create S3URIProvider for bucket: %s, key: %s after %u attempts: %s. Max attempts "
                      "reached",
                      s3_bucket.data(), s3_key.data(), retry_count_, e.what());
            throw;
        }
    }

    GST_DEBUG("Retrying to create S3URIProvider for bucket: %s, key: %s (retry #%u)", s3_bucket.data(), s3_key.data(),
              retry_count_);
    return createProvider(s3_bucket, s3_key); // Retry creating the provider
}

} // namespace gst::airtime