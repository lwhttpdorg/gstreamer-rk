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

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <aws/s3/S3Client.h>

#include "gstairtimeawsenv.hpp"
#include "gstairtimes3urichunksource.hpp"
#include "gstairtimes3urichunksourceaws.hpp"

namespace gst::airtime
{

    /// @brief Represents a single S3 object within a directory listing, along with its
    /// position in the virtual concatenated byte stream.
    struct S3URIDirectoryEntry
    {
        std::string key{};
        std::uint64_t size{0};
        std::uint64_t virtual_offset{0};
        std::string entity_tag{};
    };

    /// @brief Implements S3URIChunkSource for S3 "directories" (prefixes).
    /// Lists all objects under a given S3 prefix, concatenates them into a single virtual byte stream,
    /// and maps virtual byte ranges to physical S3 object ranges for downloading.
    class S3URIChunkSourceDirectory : public S3URIChunkSource
    {
    public:
        /// @param s3_bucket The S3 bucket name.
        /// @param s3_prefix The S3 prefix (directory path), should end with '/'.
        /// @param max_number_of_downloads The maximum number of concurrent downloads.
        /// @param http_request_timeout_ms The HTTP request timeout in milliseconds. Set to 0 for default.
        /// @param request_timeout_ms The overall request timeout in milliseconds. Set to 0 for default.
        /// @param validate_credentials Whether to validate the AWS credentials by making a STS call.
        /// @param ensure_correct_region Whether to ensure the S3 client uses the correct bucket region.
        /// @param file_pattern Glob pattern to filter files (e.g. "*.ts"). Empty means include all files.
        S3URIChunkSourceDirectory(std::string s3_bucket, std::string s3_prefix, std::size_t max_number_of_downloads,
                                  long http_request_timeout_ms, long request_timeout_ms, bool validate_credentials,
                                  bool ensure_correct_region, std::string file_pattern = "");
        ~S3URIChunkSourceDirectory() override;

        std::pair<std::error_code, S3URIObjectMetadata> getObjectMetadata() override;
        void downloadChunkAsync(S3URIChunkSpec chunk_spec, DownloadedChunkCallback callback) override;
        std::size_t activeRequests() const override;
        void cancel() override;

        /// @brief Returns the directory entries (for testing purposes).
        const std::vector<S3URIDirectoryEntry> &entries() const;

        /// @brief Describes a byte range within a single S3 object that corresponds to part of a virtual range.
        struct PhysicalRange
        {
            std::size_t entry_index{0};
            std::uint64_t object_offset{0};
            std::uint64_t size{0};
        };

        /// @brief Resolves a virtual byte range [start_byte, end_byte] to physical S3 object ranges.
        std::vector<PhysicalRange> resolveVirtualRange(std::uint64_t start_byte, std::uint64_t end_byte) const;

        /// @brief Computes a SHA-256 composite entity tag from all entries' ETags.
        std::string computeCompositeEntityTag() const;

        /// @brief Sets entries directly (for testing purposes).
        void setEntries(std::vector<S3URIDirectoryEntry> entries, std::uint64_t total_content_length);

    private:
        std::error_code listObjects();

        std::string s3_bucket_;
        std::string s3_prefix_;
        std::string file_pattern_;
        std::vector<S3URIDirectoryEntry> entries_;
        std::uint64_t total_content_length_{0};
        std::string composite_entity_tag_;

        std::shared_ptr<AwsEnv> aws_env_;
        std::shared_ptr<Aws::Utils::Threading::PooledThreadExecutor> thread_pool_executor_;
        long http_request_timeout_ms_{0};
        long request_timeout_ms_{0};
        std::shared_ptr<Aws::S3::S3Client> s3_client_;
        AwsActiveAsyncS3Requests active_async_requests_;
    };

} // namespace gst::airtime
