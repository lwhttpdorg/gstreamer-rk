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

#include <memory>
#include <mutex>
#include <vector>

#include <aws/s3/S3Client.h>

#include "gstairtimeawsenv.hpp"
#include "gstairtimes3urichunksource.hpp"

namespace gst::airtime
{

    /// @brief Creates a default S3 client configuration with virtual addressing disabled.
    /// @param thread_pool_executor The thread pool executor to use for async operations.
    /// @param http_request_timeout_ms The HTTP request timeout in milliseconds. Set to 0 for default.
    /// @param request_timeout_ms The overall request timeout in milliseconds. Set to 0 for default.
    /// @return The S3 client configuration.
    Aws::S3::S3ClientConfiguration
    createS3ClientConfiguration(std::shared_ptr<Aws::Utils::Threading::PooledThreadExecutor> thread_pool_executor,
                                long http_request_timeout_ms, long request_timeout_ms);

    /// @brief Checks the AWS credentials for S3 access. If the credentials are invalid or expired, an exception is thrown.
    void checkCredentials();

    /// @brief Ensures that the S3 client is configured for the correct region of the specified bucket. If the client is not
    /// in the correct region, it will be reconfigured.
    /// @param s3_client The S3 client to use and potentially reconfigure.
    /// @param bucket_name The name of the S3 bucket to check.
    /// @param thread_pool_executor The thread pool executor for the new client configuration.
    /// @param http_request_timeout_ms The HTTP request timeout in milliseconds.
    /// @param request_timeout_ms The overall request timeout in milliseconds.
    void ensureCorrectBucketRegion(std::shared_ptr<Aws::S3::S3Client> &s3_client, const std::string &bucket_name,
                                   std::shared_ptr<Aws::Utils::Threading::PooledThreadExecutor> thread_pool_executor,
                                   long http_request_timeout_ms, long request_timeout_ms);

    /// @brief Tracks all active S3 async requests.
    class AwsActiveAsyncS3Requests
    {
    public:
        std::shared_ptr<const Aws::Client::AsyncCallerContext> createRequestContext();

        void deleteRequestContext(std::shared_ptr<const Aws::Client::AsyncCallerContext> context);

        void clear();

        bool empty() const;

        std::size_t size() const;

    private:
        mutable std::mutex requests_access_;
        std::vector<std::shared_ptr<const Aws::Client::AsyncCallerContext>> active_requests_;
    };

    /// @brief Implements S3URIChunkSource using AWS SDK for C++.
    /// This class provides methods to download chunks from an S3 URI asynchronously.
    /// It uses the AWS S3 client to send GetObject requests with specified byte ranges.
    /// The downloaded chunks can be processed using the provided callback.
    /// The class also manages the S3 client configuration and ensures that the correct region is used for the S3 bucket.
    /// It is designed to handle multiple concurrent downloads up to a specified maximum number of downloads.
    class S3URIChunkSourceAws : public S3URIChunkSource
    {
    public:
        /// @param s3_bucket The S3 bucket name.
        /// @param s3_key The S3 object key.
        /// @param max_number_of_downloads The maximum number of concurrent downloads.
        /// @param http_request_timeout_ms The HTTP request timeout in milliseconds. Set to 0 for default.
        /// @param request_timeout_ms The overall request timeout in milliseconds. Set to 0 for default.
        /// @param validate_credentials Whether to validate the AWS credentials by making a STS call.
        /// @param ensure_correct_region Whether to ensure that the S3 client is configured for the correct region of the S3
        /// bucket.
        S3URIChunkSourceAws(std::string s3_bucket, std::string s3_key, std::size_t max_number_of_downloads,
                            long http_request_timeout_ms, long request_timeout_ms, bool validate_credentials,
                            bool ensure_correct_region);
        ~S3URIChunkSourceAws() override;

        /// @brief Uses a head object request to get the metadata of the S3 object.
        /// @return A pair containing an error code and the S3 object metadata.
        std::pair<std::error_code, S3URIObjectMetadata> getObjectMetadata() override;

        /// @brief Downloads a chunk from S3 asynchronously.
        /// This method initiates an asynchronous download of a chunk from S3.
        /// It uses the S3 client to send a GetObject request with the specified byte range.
        /// The callback will be called when the download is complete or if an error occurs.
        /// @param chunk_spec The specification of the chunk to download, including the byte range.
        /// @param callback The callback to call when the download is complete.
        /// The callback will receive an error code, the chunk specification, and a pointer to the downloaded stream.
        /// @note The callback will be called on the thread that initiated the download.
        void downloadChunkAsync(S3URIChunkSpec chunk_spec, DownloadedChunkCallback callback) override;

        /// @brief Returns the number of S3 async requests being in progress.
        /// @return The number of active S3 async requests.
        std::size_t activeRequests() const override;

        /// @brief Cancels all active S3 async requests and resets the S3 client.
        /// This method is thread-safe and can be called from any thread.
        /// It will cancel all active requests and reset the S3 client, effectively stopping any ongoing downloads.
        /// @note This method is called when the source is no longer needed or when the application is shutting down.
        /// @note It is important not to call any other methods on this object after calling this method,
        /// as it will be in an invalid state after the client is reset.
        void cancel() override;

    private:
        std::string s3_bucket_;
        std::string s3_key_;
        std::shared_ptr<AwsEnv> aws_env_;
        std::shared_ptr<Aws::Utils::Threading::PooledThreadExecutor> thread_pool_executor_;
        long http_request_timeout_ms_{0};
        long request_timeout_ms_{0};
        std::shared_ptr<Aws::S3::S3Client> s3_client_;
        AwsActiveAsyncS3Requests active_async_requests_{};
    };

} // namespace gst::airtime