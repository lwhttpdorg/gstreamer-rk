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

#include "gstairtimes3urichunksourcefake.hpp"

#include <cassert>
#include <random>
#include <sstream>
#include <thread>

#include <boost/asio/post.hpp>

namespace gst::airtime
{

namespace
{

// Generate a random integer in range [min, max]
int getRandomInt(int min, int max)
{
    // Create a random device for seeding
    static std::random_device rd;
    // Use Mersenne Twister engine
    static std::mt19937 gen(rd());
    // Define distribution
    std::uniform_int_distribution<int> distribution(min, max);
    return distribution(gen);
}

std::chrono::milliseconds generateNapTime(std::chrono::milliseconds min_nap_time,
                                          std::chrono::milliseconds max_nap_time)
{
    return std::chrono::milliseconds{getRandomInt(min_nap_time.count(), max_nap_time.count())};
}

} // namespace

S3URIChunkSourceFake::S3URIChunkSourceFake(std::string s3_bucket, std::string s3_key, std::uint64_t content_length,
                                           std::size_t max_number_of_downloads, long http_request_timeout_ms,
                                           long request_timeout_ms, std::chrono::milliseconds min_nap_time,
                                           std::chrono::milliseconds max_nap_time) :
    s3_bucket_{std::move(s3_bucket)},
    s3_key_{std::move(s3_key)},
    content_length_{content_length},
    pool_{max_number_of_downloads},
    http_request_timeout_ms_{http_request_timeout_ms},
    request_timeout_ms_{request_timeout_ms},
    min_nap_time_{min_nap_time},
    max_nap_time_{max_nap_time}
{
    assert(min_nap_time_.count() >= 0 and max_nap_time_ >= min_nap_time_);
}

S3URIChunkSourceFake::~S3URIChunkSourceFake()
{
    try
    {
        pool_.stop();
        pool_.join();
    }
    catch (std::exception&)
    {
    }
}

std::pair<std::error_code, S3URIObjectMetadata> S3URIChunkSourceFake::getObjectMetadata()
{
    return {{},
            S3URIObjectMetadata{.bucket = s3_bucket_,
                                .key = s3_key_,
                                .content_length = content_length_,
                                .entity_tag = "71d0f45dfe369cc9adacaf5c4d37ce3d", // MD5 of the word "mmhmm"
                                .version_id = "fake-version-id",
                                .expiration = "2031-01-01T00:00:00Z",
                                .last_modified = "2024-01-01T00:00:00Z"}};
}

void S3URIChunkSourceFake::downloadChunkAsync(S3URIChunkSpec chunk_spec, DownloadedChunkCallback callback)
{
    ++active_requests_;
    auto nap_time = generateNapTime(min_nap_time_, max_nap_time_);
    boost::asio::post(pool_, [this, chunk_spec = std::move(chunk_spec), callback = std::move(callback), nap_time] {
        std::this_thread::sleep_for(nap_time);
        if (http_request_timeout_ms_ > 0 and nap_time.count() > http_request_timeout_ms_)
        {
            --active_requests_;
            callback(make_error_code(std::errc::timed_out), chunk_spec, nullptr);
            return;
        }
        std::string fake_data_str(chunk_spec.actualSize(), 'a');
        std::istringstream stream(std::move(fake_data_str));
        try
        {
            callback(std::error_code{}, chunk_spec, &stream);
            --active_requests_;
        }
        catch (std::exception&)
        {
            --active_requests_;
            throw;
        }
    });
}

std::size_t S3URIChunkSourceFake::activeRequests() const
{
    return active_requests_;
}

void S3URIChunkSourceFake::cancel()
{
    pool_.stop();
    pool_.join();
}

std::unique_ptr<S3URIChunkSource>
createS3URIChunkSourceFake(std::string s3_bucket, std::string s3_key, std::uint64_t content_length,
                           std::size_t max_number_of_downloads, long http_request_timeout_ms, long request_timeout_ms,
                           std::chrono::milliseconds min_nap_time, std::chrono::milliseconds max_nap_time)
{
    return std::make_unique<S3URIChunkSourceFake>(std::move(s3_bucket), std::move(s3_key), content_length,
                                                  max_number_of_downloads, http_request_timeout_ms, request_timeout_ms,
                                                  min_nap_time, max_nap_time);
}

} // namespace gst::airtime