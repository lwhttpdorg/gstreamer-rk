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

#include "gstairtimes3urichunksourcedirectory.hpp"
#include "gstairtimes3error.hpp"
#include "gstairtimes3utils.hpp"

#include <algorithm>
#include <fnmatch.h>
#include <format>
#include <sstream>

#include <aws/core/utils/HashingUtils.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/ListObjectsV2Request.h>

#include <gst/gst.h>

namespace gst::airtime
{

    namespace
    {

        /// @brief Extracts the filename component from an S3 key (everything after the last '/').
        std::string_view extractFilename(std::string_view key)
        {
            auto last_slash = key.rfind('/');
            if (last_slash == std::string_view::npos)
            {
                return key;
            }
            return key.substr(last_slash + 1);
        }

        /// @brief Matches a filename against a glob pattern using fnmatch.
        bool matchesPattern(std::string_view filename, std::string_view pattern)
        {
            if (pattern.empty())
            {
                return true;
            }
            return fnmatch(std::string{pattern}.c_str(), std::string{filename}.c_str(), 0) == 0;
        }

    } // namespace

    S3URIChunkSourceDirectory::S3URIChunkSourceDirectory(std::string s3_bucket, std::string s3_prefix,
                                                         std::size_t max_number_of_downloads, long http_request_timeout_ms,
                                                         long request_timeout_ms, bool validate_credentials,
                                                         bool ensure_correct_region, std::string file_pattern) : s3_bucket_{std::move(s3_bucket)},
                                                                                                                 s3_prefix_{std::move(s3_prefix)},
                                                                                                                 file_pattern_{std::move(file_pattern)},
                                                                                                                 aws_env_{AwsEnvFactory::create()},
                                                                                                                 thread_pool_executor_{Aws::MakeShared<Aws::Utils::Threading::PooledThreadExecutor>(
                                                                                                                     "AIRTIMES3SRC_DIR_THREAD_POOL_EXECUTOR", max_number_of_downloads)},
                                                                                                                 http_request_timeout_ms_{http_request_timeout_ms},
                                                                                                                 request_timeout_ms_{request_timeout_ms},
                                                                                                                 s3_client_{Aws::MakeShared<Aws::S3::S3Client>(
                                                                                                                     "AIRTIMES3SRC_DIR_S3_CLIENT",
                                                                                                                     createS3ClientConfiguration(thread_pool_executor_, http_request_timeout_ms_, request_timeout_ms_))}
    {
        if (validate_credentials)
        {
            checkCredentials();
        }
        if (ensure_correct_region)
        {
            ensureCorrectBucketRegion(s3_client_, s3_bucket_, thread_pool_executor_, http_request_timeout_ms_,
                                      request_timeout_ms_);
        }
    }

    S3URIChunkSourceDirectory::~S3URIChunkSourceDirectory()
    {
        try
        {
            cancel();
        }
        catch (const std::exception &exception)
        {
            GST_ERROR("Exception during S3URIChunkSourceDirectory destruction: %s", exception.what());
        }
        catch (...)
        {
            GST_ERROR("Unknown exception during S3URIChunkSourceDirectory destruction");
        }
    }

    std::error_code S3URIChunkSourceDirectory::listObjects()
    {
        GST_DEBUG("Listing objects in bucket: %s, prefix: %s, pattern: %s", s3_bucket_.c_str(), s3_prefix_.c_str(),
                  file_pattern_.empty() ? "(all)" : file_pattern_.c_str());

        entries_.clear();
        total_content_length_ = 0;

        Aws::String continuation_token;
        bool has_more{true};

        while (has_more)
        {
            Aws::S3::Model::ListObjectsV2Request request;
            request.SetBucket(s3_bucket_);
            request.SetPrefix(s3_prefix_);
            request.SetDelimiter("/");

            if (not continuation_token.empty())
            {
                request.SetContinuationToken(continuation_token);
            }

            auto outcome = s3_client_->ListObjectsV2(request);
            if (not outcome.IsSuccess())
            {
                GST_ERROR("Error listing objects: %s", outcome.GetError().GetMessage().c_str());
                return make_error_code(outcome.GetError().GetErrorType());
            }

            auto &result = outcome.GetResult();
            for (const auto &object : result.GetContents())
            {
                const auto &key = object.GetKey();

                // Skip the prefix itself (directory marker)
                if (key == s3_prefix_)
                {
                    continue;
                }

                // Skip zero-size objects
                if (object.GetSize() <= 0)
                {
                    continue;
                }

                auto filename = extractFilename(key);
                if (not matchesPattern(filename, file_pattern_))
                {
                    continue;
                }

                S3URIDirectoryEntry entry{
                    .key = key,
                    .size = static_cast<std::uint64_t>(object.GetSize()),
                    .virtual_offset = 0,
                    .entity_tag = unescapeString(object.GetETag()),
                };
                entries_.push_back(std::move(entry));
            }

            has_more = result.GetIsTruncated();
            if (has_more)
            {
                continuation_token = result.GetNextContinuationToken();
            }
        }

        // Sort lexicographically by key (ListObjectsV2 already returns sorted, but be explicit)
        std::sort(entries_.begin(), entries_.end(),
                  [](const S3URIDirectoryEntry &left, const S3URIDirectoryEntry &right)
                  { return left.key < right.key; });

        // Compute cumulative virtual offsets
        std::uint64_t cumulative_offset{0};
        for (auto &entry : entries_)
        {
            entry.virtual_offset = cumulative_offset;
            cumulative_offset += entry.size;
        }
        total_content_length_ = cumulative_offset;

        GST_DEBUG("Listed %zu objects under prefix '%s', total content length: %" G_GUINT64_FORMAT, entries_.size(),
                  s3_prefix_.c_str(), total_content_length_);

        return {};
    }

    std::pair<std::error_code, S3URIObjectMetadata> S3URIChunkSourceDirectory::getObjectMetadata()
    {
        GST_DEBUG("Retrieving directory metadata for bucket: %s, prefix: %s", s3_bucket_.c_str(), s3_prefix_.c_str());

        auto error = listObjects();
        if (error)
        {
            return {error, {}};
        }

        if (entries_.empty())
        {
            GST_ERROR("No objects found under prefix '%s' in bucket '%s'", s3_prefix_.c_str(), s3_bucket_.c_str());
            return {make_error_code(Aws::S3::S3Errors::NO_SUCH_KEY), {}};
        }

        composite_entity_tag_ = computeCompositeEntityTag();

        S3URIObjectMetadata metadata{
            .bucket = s3_bucket_,
            .key = s3_prefix_,
            .content_length = total_content_length_,
            .entity_tag = composite_entity_tag_,
            .version_id = {},
            .expiration = {},
            .last_modified = {},
        };

        GST_INFO("Directory metadata retrieved successfully.");
        GST_DEBUG("Directory Metadata:");
        GST_DEBUG("Number of files: %zu", entries_.size());
        GST_DEBUG("Total Content Length: %" G_GUINT64_FORMAT, metadata.content_length);
        GST_DEBUG("Composite Entity Tag: %s", metadata.entity_tag.c_str());

        return {{}, std::move(metadata)};
    }

    std::vector<S3URIChunkSourceDirectory::PhysicalRange>
    S3URIChunkSourceDirectory::resolveVirtualRange(std::uint64_t start_byte, std::uint64_t end_byte) const
    {
        std::vector<PhysicalRange> ranges;

        if (entries_.empty() or start_byte > end_byte)
        {
            return ranges;
        }

        // Binary search: find the first entry whose virtual_offset + size > start_byte
        auto entry_iter = std::upper_bound(
            entries_.begin(), entries_.end(), start_byte,
            [](std::uint64_t value, const S3URIDirectoryEntry &entry)
            { return value < entry.virtual_offset; });

        // upper_bound gives us the first entry with virtual_offset > start_byte,
        // so we need to go back one to find the entry containing start_byte
        if (entry_iter != entries_.begin())
        {
            --entry_iter;
        }

        while (entry_iter != entries_.end() and entry_iter->virtual_offset <= end_byte)
        {
            const auto &entry = *entry_iter;
            const std::uint64_t entry_end = entry.virtual_offset + entry.size - 1;

            // Check if this entry actually overlaps with [start_byte, end_byte]
            if (entry_end < start_byte)
            {
                ++entry_iter;
                continue;
            }

            const auto entry_index = static_cast<std::size_t>(std::distance(entries_.cbegin(), entry_iter));
            const std::uint64_t range_start = std::max(start_byte, entry.virtual_offset);
            const std::uint64_t range_end = std::min(end_byte, entry_end);

            PhysicalRange range{
                .entry_index = entry_index,
                .object_offset = range_start - entry.virtual_offset,
                .size = range_end - range_start + 1,
            };
            ranges.push_back(range);

            ++entry_iter;
        }

        return ranges;
    }

    std::string S3URIChunkSourceDirectory::computeCompositeEntityTag() const
    {
        // Concatenate all entry ETags with '-' separator, then SHA-256 hash the result
        std::string concatenated_tags;
        for (std::size_t index{0}; index < entries_.size(); ++index)
        {
            if (index > 0)
            {
                concatenated_tags += '-';
            }
            concatenated_tags += entries_[index].entity_tag;
        }

        auto hash = Aws::Utils::HashingUtils::CalculateSHA256(concatenated_tags.c_str());
        return Aws::Utils::HashingUtils::HexEncode(hash);
    }

    void S3URIChunkSourceDirectory::downloadChunkAsync(S3URIChunkSpec chunk_spec, DownloadedChunkCallback callback)
    {
        auto physical_ranges = resolveVirtualRange(chunk_spec.startByte(), chunk_spec.endByte());

        if (physical_ranges.empty())
        {
            callback(make_error_code(Aws::S3::S3Errors::NO_SUCH_KEY), std::move(chunk_spec), nullptr);
            return;
        }

        if (physical_ranges.size() == 1)
        {
            // Single-object case: issue a single GetObject request
            const auto &range = physical_ranges[0];
            const auto &entry = entries_[range.entry_index];

            GST_DEBUG("%s", std::format("Downloading chunk from single object, bucket: {}, key: {}, bytes={}-{}",
                                        s3_bucket_, entry.key, range.object_offset, range.object_offset + range.size - 1)
                                .c_str());

            Aws::S3::Model::GetObjectRequest request;
            request.SetBucket(s3_bucket_);
            request.SetKey(entry.key);
            request.SetRange(std::format("bytes={}-{}", range.object_offset, range.object_offset + range.size - 1));

            auto context = active_async_requests_.createRequestContext();
            s3_client_->GetObjectAsync(
                request,
                [this, chunk_spec = std::move(chunk_spec),
                 callback = std::move(callback)](const Aws::S3::S3Client *, const Aws::S3::Model::GetObjectRequest &,
                                                 const Aws::S3::Model::GetObjectOutcome &outcome,
                                                 const std::shared_ptr<const Aws::Client::AsyncCallerContext> &context)
                {
                    try
                    {
                        if (outcome.IsSuccess())
                        {
                            auto &result = outcome.GetResult();
                            Aws::IOStream &body = result.GetBody();
                            callback(std::error_code{}, std::move(chunk_spec), &body);
                        }
                        else
                        {
                            callback(make_error_code(outcome.GetError().GetErrorType()), std::move(chunk_spec), nullptr);
                        }
                    }
                    catch (const std::exception &exception)
                    {
                        GST_ERROR("Exception in directory download callback: %s", exception.what());
                    }
                    catch (...)
                    {
                        GST_ERROR("Unknown exception in directory download callback");
                    }
                    active_async_requests_.deleteRequestContext(context);
                },
                context);
        }
        else
        {
            // Boundary-crossing case: download multiple parts and concatenate
            GST_DEBUG("Downloading chunk spanning %zu objects (boundary crossing)", physical_ranges.size());

            auto context = active_async_requests_.createRequestContext();

            // Use the thread pool to perform the concatenated download
            thread_pool_executor_->Submit([this, physical_ranges = std::move(physical_ranges),
                                           chunk_spec = std::move(chunk_spec), callback = std::move(callback),
                                           context]() mutable
                                          {
            try
            {
                auto combined_stream = std::make_shared<std::stringstream>();

                for (const auto& range : physical_ranges)
                {
                    const auto& entry = entries_[range.entry_index];

                    Aws::S3::Model::GetObjectRequest request;
                    request.SetBucket(s3_bucket_);
                    request.SetKey(entry.key);
                    request.SetRange(
                        std::format("bytes={}-{}", range.object_offset, range.object_offset + range.size - 1));

                    GST_DEBUG("%s", std::format("Boundary download: bucket: {}, key: {}, bytes={}-{}", s3_bucket_,
                                                entry.key, range.object_offset, range.object_offset + range.size - 1)
                                        .c_str());

                    auto outcome = s3_client_->GetObject(request);
                    if (not outcome.IsSuccess())
                    {
                        GST_ERROR("Error downloading part from key '%s': %s", entry.key.c_str(),
                                  outcome.GetError().GetMessage().c_str());
                        callback(make_error_code(outcome.GetError().GetErrorType()), std::move(chunk_spec), nullptr);
                        active_async_requests_.deleteRequestContext(context);
                        return;
                    }

                    auto& body = outcome.GetResult().GetBody();
                    *combined_stream << body.rdbuf();
                }

                combined_stream->seekg(0);
                callback(std::error_code{}, std::move(chunk_spec), combined_stream.get());
            }
            catch (const std::exception& exception)
            {
                GST_ERROR("Exception in boundary-crossing download: %s", exception.what());
                callback(make_error_code(Aws::S3::S3Errors::INTERNAL_FAILURE), std::move(chunk_spec), nullptr);
            }
            catch (...)
            {
                GST_ERROR("Unknown exception in boundary-crossing download");
                callback(make_error_code(Aws::S3::S3Errors::INTERNAL_FAILURE), std::move(chunk_spec), nullptr);
            }
            active_async_requests_.deleteRequestContext(context); });
        }
    }

    std::size_t S3URIChunkSourceDirectory::activeRequests() const
    {
        return active_async_requests_.size();
    }

    void S3URIChunkSourceDirectory::cancel()
    {
        thread_pool_executor_->WaitUntilStopped();
        s3_client_.reset();
        active_async_requests_.clear();
        GST_DEBUG("All S3 directory operations cancelled");
    }

    const std::vector<S3URIDirectoryEntry> &S3URIChunkSourceDirectory::entries() const
    {
        return entries_;
    }

    void S3URIChunkSourceDirectory::setEntries(std::vector<S3URIDirectoryEntry> entries, std::uint64_t total_content_length)
    {
        entries_ = std::move(entries);
        total_content_length_ = total_content_length;
        composite_entity_tag_ = computeCompositeEntityTag();
    }

} // namespace gst::airtime
