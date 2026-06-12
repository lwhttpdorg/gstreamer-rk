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
#include <filesystem>
#include <string>
#include <string_view>

namespace gst::airtime
{

    enum class SourceHint
    {
        none,
        key,
        prefix
    };

    inline constexpr std::uint64_t default_file_cache_chunk_size_bytes = (1024 * 1024);                           // 1 MB
    inline constexpr std::uint64_t default_download_chunk_size_bytes = (default_file_cache_chunk_size_bytes * 2); // 2 MB
    inline constexpr std::uint64_t default_number_of_concurrent_downloads = 25;                                   // AWS C++ SDK default is 25

    struct S3URIProviderConfig
    {
        /// @brief The size of the chunks to download from S3.
        std::uint64_t download_chunk_size{default_download_chunk_size_bytes};

        /// @brief The size of the file chunks to use for caching/local storage.
        std::uint64_t file_chunk_size{default_file_cache_chunk_size_bytes};

        /// @brief The maximum number of concurrent S3 chunk downloads to cache the S3 object locally.
        std::uint64_t max_number_of_downloads{default_number_of_concurrent_downloads};

        /// @brief For testing purposes, use a fake S3 source instead of the real one.
        bool use_fake_aws_source{false};

        /// @brief The base directory for the cache.
        /// This is used to store the cached S3 objects and their metadata.
        /// The default value is a temporary directory subdirectory.
        std::filesystem::path cache_base_directory{std::filesystem::temp_directory_path() / "airtime_s3_cache"};

        /// @brief The maximum size of the cache in bytes.
        /// This is used to limit the size of the cache directory.
        /// If the cache exceeds this size, the oldest keys will be purged.
        /// If the cache size is set to 0, no size limit is applied.
        /// The default value is 10 GB.
        std::uint64_t max_cache_size_bytes{std::uint64_t{10} * 1024 * 1024 * 1024}; // 10 GB

        /// @brief The maximum number of retry attempts to download a chunk.
        /// If the download fails, it will be retried up to this number of times.
        unsigned fetch_max_retry_count = 2;

        /// @brief Whether to trust the cached data.
        /// If this is set to true, the cached metadata will be used without checking
        /// for updates (if exists) avoiding unnecessary S3 requests. If set to false, the metadata will be re-fetched from
        /// S3.
        bool trust_cached_data = false;

        /// @brief The timeout for HTTP requests in milliseconds. Corresponds to the AWS client configuration
        /// `httpRequestTimeoutMs` property.
        /// @note A value of `0` means no timeout.
        long http_request_timeout_ms = 0;

        /// @brief The timeout for socket requests in milliseconds. Corresponds to the AWS client configuration
        /// `requestTimeoutMs` property.
        long request_timeout_ms = 0;

        /// @brief Validate the AWS credentials by making a STS call.
        bool validate_credentials = false;

        /// @brief Ensure that the S3 client is configured for the correct region of the S3 bucket.
        bool ensure_correct_region = false;

        /// @brief Whether to elevate S3 errors to std::cout in addition to logging them with GST_ERROR.
        /// This is useful because GST_ERROR logs may not be visible to the user, whereas std::cout output is.
        /// Specifically, this helps to provide context for errors reported by GStreamer's discovery mechanism.
        bool elevate_s3_errors_to_cout = false;

        /// @brief Hint for whether the S3 URI points to a single object (key) or a directory (prefix).
        /// When set to `none`, the element will auto-detect by trying HeadObject first, then ListObjectsV2.
        SourceHint source_hint{SourceHint::none};

        /// @brief Glob pattern to filter files when operating in prefix/directory mode (e.g. "*.ts", "segment_*").
        /// Empty string means include all files under the prefix.
        std::string file_pattern;
    };

} // namespace gst::airtime