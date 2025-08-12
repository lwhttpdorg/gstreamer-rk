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

#include "gstairtimes3uriprovider.hpp"
#include "gstairtimes3error.hpp"
#include "gstairtimes3urichunksourceaws.hpp"

#include <cinttypes>
#include <format>
#include <fstream>
#include <ranges>

#include <gst/gst.h>

namespace gst::airtime
{

S3URIProvider::S3URIProvider(std::string_view s3_bucket, std::string_view s3_key, S3URIProviderConfig config,
                             std::unique_ptr<S3URIChunkSource> chunk_source,
                             std::unique_ptr<S3URIChunkProcessor> chunk_processor) :
    config_{config},
    s3_bucket_{s3_bucket},
    s3_key_{s3_key},
    chunk_source_{std::move(chunk_source)},
    chunk_processor_{std::move(chunk_processor)}
{
    assert(chunk_source_);
    assert(chunk_processor_);
    getS3URIObjectAsync();
}

S3URIProvider::~S3URIProvider()
{
    try
    {
        cancelAsyncDownload();
    }
    catch (const std::exception&)
    {
    }
}

bool S3URIProvider::hasError() const
{
    std::lock_guard lock{error_access_};
    return error_type_.has_value();
}

std::optional<S3URIProvider::ErrorType> S3URIProvider::getErrorType() const
{
    std::lock_guard lock{error_access_};
    return error_type_;
}

std::optional<std::string> S3URIProvider::getLastErrorMessage() const
{
    std::lock_guard lock{error_access_};
    if (error_type_.has_value())
    {
        return last_error_message_;
    }
    return std::nullopt;
}

std::uint64_t S3URIProvider::getContentLength() const
{
    return s3_object_metadata_.content_length;
}

bool S3URIProvider::waitForAllChunksDownloaded(std::chrono::seconds timeout)
{
    return waitForRangeDownloaded(0, s3_object_metadata_.content_length, timeout);
}

bool S3URIProvider::waitForRangeDownloaded(std::uint64_t offset, std::uint64_t size, std::chrono::seconds timeout)
{
    if (offset + size > s3_object_metadata_.content_length)
    {
        throw std::runtime_error("Requested range exceeds content length");
    }
    if (downloaded_chunk_notifier_)
    {
        if (not downloaded_chunk_notifier_->waitForRangeAvailable(offset, size, timeout))
        {
            return false;
        }
    }
    if (auto error = getLastErrorMessage())
    {
        throw std::runtime_error(
            std::format("S3URIProvider is in an error state, cannot wait for range. Last error: {}", *error));
    }
    return true;
}

std::pair<S3BufferFillStatus, std::uint64_t> S3URIProvider::fill(std::uint8_t* data, std::uint64_t offset,
                                                                 std::uint64_t size, std::chrono::seconds timeout)
{
    // Check if the queue has been created. In case the chunks were already downloaded and taken from cache,
    // the queue is not created.
    if (download_chunk_queue_)
    {
        download_chunk_queue_->setHighestPriorityForRange(offset, size);
        if (not waitForRangeDownloaded(offset, size, timeout))
        {
            throw std::runtime_error(
                std::format("Timeout waiting for range {}-{} to be downloaded", offset, offset + size));
        }
    }
    if (auto error = getLastErrorMessage())
    {
        throw std::runtime_error(std::format(
            "S3URIProvider is in an error state, cannot wait for range and cannot fill the buffer. Last error: {}",
            *error));
    }
    return chunk_processor_->fill(data, offset, size);
}

S3URIProvider::DownloadCompletionState S3URIProvider::getDownloadCompletionState() const
{
    return download_completion_state_.load();
}

std::string_view S3URIProvider::getDownloadCompletionStateString() const noexcept
{
    return downloadCompletionStateToString(getDownloadCompletionState());
}

std::uint64_t S3URIProvider::getMetrics() const
{
    return fetch_duration_.load();
}

void S3URIProvider::getS3URIObjectAsync()
{
    // First we check if the file chunks are still available in the cache directory:
    // 1) Obtain S3 object metadata of the S3 object, in particular content length and MD5 hash (if needed the call goes
    // to S3). This is used to:
    //  * determine the number of chunks to fetch and to set the content length in the cache.
    //  * verify the integrity of the downloaded object and to determine if the cache can be used.
    //    If the MD5 hash matches the one in the cache, we can restore the S3 object from the cache.
    //    If the MD5 hash does not match, we need to download the S3 object again.
    // 2) Check if the chunks are already available (for instance in a cache).
    //   - If the chunks are available, we can restore the S3 object from the cache.
    //   - If the chunks are not available, we need to download the S3 object in chunks and cache it.
    // 3) If at any point of preparing the cache the cache inconsistency is detected, the S3 downloading procedure
    // starts from scratch.

    processContentLength();

    const auto& [needs_chunk, file_chunk_gaps] = chunk_processor_->needsChunks();
    if (not needs_chunk)
    {
        // object is already cached, so we can just set the status to cached
        GST_DEBUG("Fully restored S3 object local cache");
        download_completion_state_ = DownloadCompletionState::fully_cached;
        return;
    }
    else if (not file_chunk_gaps.empty())
    {
        GST_DEBUG("Partially restored S3 object local cache");
        download_completion_state_ = DownloadCompletionState::partially_cached;
    }

    // Start downloading the object chunks
    // Chunks are being fetched asynchronously, up to the specified max number of parallel downloads at once.
    startDownloadingS3ObjectChunks(file_chunk_gaps);
}

void S3URIProvider::processContentLength()
{
    if (chunk_processor_->needsObjectMetadata())
    {
        auto [error, s3_object_metadata] = chunk_source_->getObjectMetadata();
        if (error)
        {
            throw std::system_error(error, "Error retrieving S3 object metadata");
        }
        chunk_processor_->setObjectMetadata(s3_object_metadata);
        s3_object_metadata_ = std::move(s3_object_metadata);
    }
    else
    {
        s3_object_metadata_ = chunk_processor_->getObjectMetadata();
    }
}

void S3URIProvider::startDownloadingS3ObjectChunks(const std::vector<S3URIFileChunkGapSpec>& file_chunk_gaps)
{
    GST_DEBUG("download from S3: %s/%s", s3_bucket_.c_str(), s3_key_.c_str());
    downloaded_chunk_notifier_ = std::make_unique<S3URIDownloadedChunkNotifier>();
    download_chunk_queue_ = std::make_unique<S3URIChunkDownloadQueue>(
        s3_object_metadata_.content_length, config_.download_chunk_size,
        [this, &file_chunk_gaps](const S3URIChunkSpec& chunk_spec) {
            const bool pass = passFileChunkGap(config_.download_chunk_size, config_.file_chunk_size, chunk_spec.index(),
                                               file_chunk_gaps);
            if (not pass)
            {
                // the chunk is already cached, so we can skip downloading it and inform the notifier
                GST_DEBUG("Skipping download of chunk %zu, already cached", chunk_spec.index());
                downloaded_chunk_notifier_->notifyChunkDownloaded(chunk_spec.startByte(), chunk_spec.actualSize());
            }
            return pass;
        });
    for (std::size_t i = 0; i < config_.max_number_of_downloads; ++i)
    {
        // get the next chunk to download
        auto chunk = download_chunk_queue_->getNextChunk();
        if (not chunk.has_value())
        {
            // no more chunks to download
            break;
        }
        initiateAsyncDownload(std::move(*chunk), 0);
    }
}

void S3URIProvider::initiateAsyncDownload(S3URIChunkSpec chunk_spec, unsigned retry_count)
{
    chunk_source_->downloadChunkAsync(chunk_spec, [this, retry_count](std::error_code error, S3URIChunkSpec chunk_spec,
                                                                      std::istream* stream) mutable {
        try
        {
            if (error)
            {
                static const auto network_connection_timeout_error_code =
                    make_error_code(Aws::S3::S3Errors::NETWORK_CONNECTION);
                if (error == network_connection_timeout_error_code and not hasError())
                {
                    // retry the download if we have not reached the max retry count
                    if (++retry_count <= config_.fetch_max_retry_count)
                    {
                        GST_WARNING(
                            "Network connection error while downloading chunk %zu from S3: %s. Retrying (%u/%u)",
                            chunk_spec.index(), error.message().c_str(), retry_count, config_.fetch_max_retry_count);
                        initiateAsyncDownload(std::move(chunk_spec), retry_count);
                        return;
                    }
                    else
                    {
                        GST_WARNING("Max retry count (%u) reached for chunk %zu, giving up",
                                    config_.fetch_max_retry_count, chunk_spec.index());
                    }
                }
                setError(ErrorType::download,
                         std::format("Failed to download chunk {} from S3: {}", chunk_spec.index(), error.message()));
                return;
            }
            assert(stream);
            chunk_processor_->processChunk(chunk_spec, *stream);
            downloaded_chunk_notifier_->notifyChunkDownloaded(chunk_spec.startByte(), chunk_spec.actualSize());

            if (hasError())
            {
                // If we are in an error state, we should not process next chunks
                if (chunk_source_->activeRequests() == 0)
                {
                    const std::chrono::nanoseconds duration_ns = std::chrono::steady_clock::now() - fetch_start_time_;
                    fetch_duration_.store(duration_ns.count());
                    GST_DEBUG(
                        "Chunks downloaded in %lld seconds",
                        static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(duration_ns).count()));
                }
                return;
            }

            // get the next chunk to download
            auto chunk = download_chunk_queue_->getNextChunk();
            if (chunk.has_value())
            {
                initiateAsyncDownload(std::move(*chunk), 0);
            }
            else
            {
                if (chunk_source_->activeRequests() == 0)
                {
                    const std::chrono::nanoseconds duration_ns = std::chrono::steady_clock::now() - fetch_start_time_;
                    fetch_duration_.store(duration_ns.count());
                    download_completion_state_ = DownloadCompletionState::fully_downloaded;
                    GST_DEBUG(
                        "All chunks downloaded in %lld seconds",
                        static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(duration_ns).count()));
                    if (not chunk_processor_->allChunksProcessed())
                    {
                        setError(ErrorType::processor, "Error in processor consistency");
                    }
                }
            }
        }
        catch (const std::exception& e)
        {
            setError(ErrorType::other, std::format("Error in async download callback: {}", e.what()));
        }
        catch (...)
        {
            setError(ErrorType::other, "Unknown error in async download callback");
        }
    });
}

void S3URIProvider::cancelAsyncDownload()
{
    if (chunk_source_)
    {
        chunk_source_->cancel();
    }
    GST_DEBUG("All S3 operations cancelled");
}

void S3URIProvider::setError(ErrorType type, std::string message)
{
    GST_ERROR("S3URIProvider error: type: %d, message: %s", static_cast<int>(type), message.c_str());
    {
        std::lock_guard lock{error_access_};
        error_type_ = type;
        download_completion_state_ = DownloadCompletionState::download_failed;
        last_error_message_ = std::move(message);
    }
    if (downloaded_chunk_notifier_)
    {
        downloaded_chunk_notifier_->stopWaitingForRange();
    }
}

std::unique_ptr<S3URICacheEvictionPolicy> createEvictionPolicy(const S3URIProviderConfig& config)
{
    if (config.max_cache_size_bytes == 0)
    {
        return std::make_unique<NoRulesCacheEvictionPolicy>();
    }
    return std::make_unique<LRUCacheEvictionPolicy>(config.max_cache_size_bytes);
}

std::unique_ptr<S3URIChunkSource> createChunkSource(std::string_view s3_bucket, std::string_view s3_key,
                                                    const S3URIProviderConfig& config)
{
    if (config.use_fake_aws_source)
    {
        GST_DEBUG("Using fake S3 source for testing purposes.");
        return createS3URIChunkSourceFake(100 * 1024 * 1024, config.max_number_of_downloads);
    }
    else
    {
        GST_DEBUG("Using real S3 source with bucket: %s, key: %s", s3_bucket.data(), s3_key.data());
        return std::make_unique<S3URIChunkSourceAws>(std::string{s3_bucket}, std::string{s3_key},
                                                     config.max_number_of_downloads);
    }
}

std::shared_ptr<S3URIProvider> createS3URIProvider(std::shared_ptr<S3URICacheManager> cache_manager,
                                                   std::string_view s3_bucket, std::string_view s3_key,
                                                   const S3URIProviderConfig& config)
{
    auto chunk_source = createChunkSource(s3_bucket, s3_key, config);
    auto chunk_processor = std::make_unique<gst::airtime::CachingS3URIChunkProcessor>(cache_manager, s3_bucket, s3_key,
                                                                                      config.file_chunk_size);
    return std::make_shared<S3URIProvider>(s3_bucket, s3_key, config, std::move(chunk_source),
                                           std::move(chunk_processor));
}

std::string_view downloadCompletionStateToString(S3URIProvider::DownloadCompletionState state) noexcept
{
    switch (state)
    {
        case S3URIProvider::DownloadCompletionState::in_progress:
            return "in-progress";
        case S3URIProvider::DownloadCompletionState::fully_downloaded:
            return "fully-downloaded";
        case S3URIProvider::DownloadCompletionState::partially_cached:
            return "partially-cached";
        case S3URIProvider::DownloadCompletionState::fully_cached:
            return "fully-cached";
        case S3URIProvider::DownloadCompletionState::download_failed:
            return "download-failed";
        default:
            return "unknown";
    }
}

namespace
{

/// @brief Check if two ranges overlap.
/// @param from1 Start of the first range
/// @param to1 End of the first range
/// @param from2 Start of the second range
/// @param to2 End of the second range
/// @return true if the ranges overlap, false otherwise
bool areRangesOverlapping(std::uint64_t from1, std::uint64_t to1, std::uint64_t from2, std::uint64_t to2) noexcept
{
    // Two ranges overlap if one range doesn't end before the other starts
    return from1 <= to2 && from2 <= to1;
}

} // namespace

bool passFileChunkGap(std::uint64_t download_chunk_standard_size, std::uint64_t file_chunk_standard_size,
                      std::size_t download_chunk_index, const std::vector<S3URIFileChunkGapSpec>& file_chunk_gaps)
{
    if (download_chunk_standard_size < file_chunk_standard_size)
    {
        throw std::runtime_error("Download chunk size must not be less than file chunk size");
    }
    if (download_chunk_standard_size % file_chunk_standard_size != 0)
    {
        throw std::invalid_argument{
            std::format("Download chunk standard size {} is not a multiple of file chunk standard size {}",
                        download_chunk_standard_size, file_chunk_standard_size)};
    }

    if (file_chunk_gaps.empty())
    {
        return true;
    }

    const std::uint64_t download_chunk_start_byte = download_chunk_index * download_chunk_standard_size;
    const std::uint64_t download_chunk_end_byte = download_chunk_start_byte + download_chunk_standard_size - 1;
    for (const auto& file_chunk_gap : file_chunk_gaps)
    {
        const bool pass = std::visit(
            [&download_chunk_start_byte, download_chunk_end_byte, file_chunk_standard_size](const auto& gap_spec) {
                using T = std::decay_t<decltype(gap_spec)>;

                if constexpr (std::is_same_v<T, S3URIFileChunkGapIndex>)
                {
                    const std::uint64_t file_chunk_start_byte = gap_spec.index * file_chunk_standard_size;
                    const std::uint64_t file_chunk_end_byte = file_chunk_start_byte + file_chunk_standard_size - 1;
                    return areRangesOverlapping(file_chunk_start_byte, file_chunk_end_byte, download_chunk_start_byte,
                                                download_chunk_end_byte);
                }
                else if constexpr (std::is_same_v<T, S3URIFileChunkGapIndicesRange>)
                {
                    const std::uint64_t file_chunk_start_byte = gap_spec.from * file_chunk_standard_size;
                    const std::uint64_t file_chunk_end_byte =
                        gap_spec.to * file_chunk_standard_size + file_chunk_standard_size - 1;
                    return areRangesOverlapping(file_chunk_start_byte, file_chunk_end_byte, download_chunk_start_byte,
                                                download_chunk_end_byte);
                }
            },
            file_chunk_gap);
        if (pass)
        {
            return true;
        }
    }
    return false;
}

} // namespace gst::airtime