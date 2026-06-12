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

#include "gstairtimes3urichunksource.hpp"

#include <atomic>
#include <chrono>

#include <boost/asio/thread_pool.hpp>

namespace gst::airtime
{

/// @brief A fake implementation of S3URIChunkSource for testing purposes.
class S3URIChunkSourceFake : public S3URIChunkSource
{
public:
    S3URIChunkSourceFake(std::string s3_bucket, std::string s3_key, std::uint64_t content_length,
                         std::size_t max_number_of_downloads, long http_request_timeout_ms, long request_timeout_ms,
                         std::chrono::milliseconds min_nap_time, std::chrono::milliseconds max_nap_time);

    ~S3URIChunkSourceFake() override;

    std::pair<std::error_code, S3URIObjectMetadata> getObjectMetadata() override;

    void downloadChunkAsync(S3URIChunkSpec chunk_spec, DownloadedChunkCallback callback) override;

    std::size_t activeRequests() const override;

    void cancel() override;

private:
    std::string s3_bucket_;
    std::string s3_key_;
    std::uint64_t content_length_{0}; // Content length of the fake S3 object
    boost::asio::thread_pool pool_;
    long http_request_timeout_ms_{0};             // HTTP request timeout in milliseconds
    long request_timeout_ms_{0};                  // Request timeout in milliseconds
    std::atomic<std::size_t> active_requests_{0}; // Number of active requests
    std::chrono::milliseconds min_nap_time_;
    std::chrono::milliseconds max_nap_time_;
};

} // namespace gst::airtime