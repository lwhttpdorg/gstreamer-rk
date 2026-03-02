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
#include "gstairtimecleanup.hpp"
#include "gstairtimes3error.hpp"
#include "gstairtimes3urichunksourceaws.hpp"
#include "gstairtimes3urichunksourcedirectory.hpp"

#include <cinttypes>
#include <format>
#include <fstream>
#include <ranges>

#include <gst/gst.h>

namespace gst::airtime
{

    S3URIProvider::S3URIProvider(std::string_view s3_bucket, std::string_view s3_key, S3URIProviderConfig config,
                                 std::unique_ptr<S3URIChunkSource> chunk_source,
                                 std::unique_ptr<S3URIChunkProcessor> chunk_processor) : config_{config},
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
        catch (const std::exception &)
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

    std::pair<S3BufferFillStatus, std::uint64_t> S3URIProvider::fill(std::uint8_t *data, std::uint64_t offset,
                                                                     std::uint64_t size, std::chrono::seconds timeout)
    {
        // Check if the queue has been created. In case the chunks were already downloaded and taken from cache,
        // the queue is not created.
        if (download_chunk_queue_)
        {
            download_chunk_queue_->prioritizeRange(offset, size);
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

        const auto &[needs_chunk, file_chunk_gaps] = chunk_processor_->needsChunks();
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

    void S3URIProvider::startDownloadingS3ObjectChunks(const std::vector<S3URIFileChunkGapSpec> &file_chunk_gaps)
    {
        GST_DEBUG("download from S3: %s/%s", s3_bucket_.c_str(), s3_key_.c_str());
        downloaded_chunk_notifier_ = std::make_unique<S3URIDownloadedChunkNotifier>();
        download_chunk_queue_ = std::make_unique<S3URIChunkDownloadQueue>(
            s3_object_metadata_.content_length, config_.download_chunk_size,
            [this, &file_chunk_gaps](const S3URIChunkSpec &chunk_spec)
            {
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
            active_requests_count_++;
            initiateAsyncDownload(std::move(*chunk), 0);
        }
    }

    void S3URIProvider::initiateAsyncDownload(S3URIChunkSpec chunk_spec, unsigned retry_count)
    {
        chunk_source_->downloadChunkAsync(chunk_spec, [this, retry_count](std::error_code error, S3URIChunkSpec chunk_spec,
                                                                          std::istream *stream) mutable
                                          {
#if defined(__GNUC__) and not defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif // __GNUC__
        Cleanup decrease_active_requests_count = [this] {
            assert(active_requests_count_ > 0);
            active_requests_count_--;
        };
#if defined(__GNUC__) and not defined(__clang__)
#pragma GCC diagnostic pop
#endif // __GNUC__
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
                        std::move(decrease_active_requests_count).cancel();
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

            Cleanup chunk_ready_notifier = [this, &chunk_spec] {
                downloaded_chunk_notifier_->notifyChunkDownloaded(chunk_spec.startByte(), chunk_spec.actualSize());
            };

            if (hasError())
            {
                // If we are in an error state, we do not process next chunks but set processing time once the last
                // active request is done
                std::move(decrease_active_requests_count)
                    .invoke(); // forcing decrease here and checking if this is the last one

                int last_active_request_counter_value = 0;
                const bool last_request_finished =
                    active_requests_count_.compare_exchange_strong(last_active_request_counter_value, -1);
                if (last_request_finished)
                {
                    // active_requests_count_ went down from 0 -> -1 meaning this is the last active request.
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
                active_requests_count_++;
                initiateAsyncDownload(std::move(*chunk), 0);
            }
            else
            {
                assert(active_requests_count_ > 0);

                std::move(decrease_active_requests_count)
                    .invoke(); // forcing decrease here and checking if this is the last one

                int last_active_request_counter_value = 0;
                // It might happen that two last callbacks entered simultaneously here, but we want to call the code
                // for the last_request_finished condition set to tru just once
                const bool last_request_finished =
                    active_requests_count_.compare_exchange_strong(last_active_request_counter_value, -1);
                if (last_request_finished)
                {
                    // active_requests_count_ went down from 0 -> -1 meaning this is the last active request and all
                    // previous or currently running callbacks have called chunk_processor_->processChunk, so
                    // chunk_processor_ has all the chunks.
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
        } });
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

    std::unique_ptr<S3URICacheEvictionPolicy> createEvictionPolicy(const S3URIProviderConfig &config)
    {
        if (config.max_cache_size_bytes == 0)
        {
            return std::make_unique<NoRulesCacheEvictionPolicy>();
        }
        return std::make_unique<LRUCacheEvictionPolicy>(config.max_cache_size_bytes);
    }

    std::unique_ptr<S3URIChunkSource> createDirectoryChunkSource(std::string_view s3_bucket, std::string_view s3_prefix,
                                                                 const S3URIProviderConfig &config)
    {
        GST_DEBUG("Using directory S3 source with bucket: %s, prefix: %s", std::string{s3_bucket}.data(),
                  std::string{s3_prefix}.data());
        return std::make_unique<S3URIChunkSourceDirectory>(
            std::string{s3_bucket}, std::string{s3_prefix}, config.max_number_of_downloads, config.http_request_timeout_ms,
            config.request_timeout_ms, config.validate_credentials, config.ensure_correct_region, config.file_pattern);
    }

    std::unique_ptr<S3URIChunkSource> createSingleKeyChunkSource(std::string_view s3_bucket, std::string_view s3_key,
                                                                 const S3URIProviderConfig &config)
    {
        GST_DEBUG("Using file S3 source with bucket: %s, key: %s", std::string{s3_bucket}.data(),
                  std::string{s3_key}.data());
        return std::make_unique<S3URIChunkSourceAws>(
            std::string{s3_bucket}, std::string{s3_key}, config.max_number_of_downloads, config.http_request_timeout_ms,
            config.request_timeout_ms, config.validate_credentials, config.ensure_correct_region);
    }

    std::unique_ptr<S3URIChunkSource> createChunkSource(std::string_view s3_bucket, std::string_view s3_key,
                                                        const S3URIProviderConfig &config)
    {
        if (config.use_fake_aws_source)
        {
            GST_DEBUG("Using fake S3 source for testing purposes.");
            return createS3URIChunkSourceFake(std::string{s3_bucket}, std::string{s3_key}, 100 * 1024 * 1024,
                                              config.max_number_of_downloads, config.http_request_timeout_ms,
                                              config.request_timeout_ms, std::chrono::milliseconds{10},
                                              std::chrono::milliseconds{30});
        }

        switch (config.source_hint)
        {
        case SourceHint::key:
            return createSingleKeyChunkSource(s3_bucket, s3_key, config);

        case SourceHint::prefix:
            return createDirectoryChunkSource(s3_bucket, s3_key, config);

        case SourceHint::none:
        default:
        {
            // Auto-detect: trailing slash means directory
            if (not s3_key.empty() and s3_key.back() == '/')
            {
                GST_DEBUG("Auto-detected directory source (trailing slash) for key: %s", std::string{s3_key}.data());
                return createDirectoryChunkSource(s3_bucket, s3_key, config);
            }

            // Try HeadObject first to see if it's a single key
            GST_DEBUG("Auto-detecting source type for bucket: %s, key: %s", std::string{s3_bucket}.data(),
                      std::string{s3_key}.data());
            try
            {
                auto single_source = createSingleKeyChunkSource(s3_bucket, s3_key, config);
                auto [error, metadata] = single_source->getObjectMetadata();
                if (not error)
                {
                    GST_DEBUG("Auto-detected single key source for key: %s", std::string{s3_key}.data());
                    return single_source;
                }

                // HeadObject failed — try as a directory prefix
                GST_DEBUG("HeadObject failed for key '%s' (%s), trying as directory prefix...",
                          std::string{s3_key}.data(), error.message().c_str());
            }
            catch (const std::exception &exception)
            {
                GST_DEBUG("HeadObject probe threw for key '%s' (%s), trying as directory prefix...",
                          std::string{s3_key}.data(), exception.what());
            }

            // Append '/' if not present and try as prefix
            std::string prefix{s3_key};
            if (prefix.empty() or prefix.back() != '/')
            {
                prefix += '/';
            }
            return createDirectoryChunkSource(s3_bucket, prefix, config);
        }
        }
    }

    std::shared_ptr<S3URIProvider> createS3URIProvider(std::shared_ptr<S3URICacheManager> cache_manager,
                                                       std::string_view s3_bucket, std::string_view s3_key,
                                                       const S3URIProviderConfig &config)
    {
        auto chunk_source = createChunkSource(s3_bucket, s3_key, config);
        auto chunk_processor = std::make_unique<gst::airtime::CachingS3URIChunkProcessor>(
            cache_manager, s3_bucket, s3_key, config.file_chunk_size, config.trust_cached_data);
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

    bool passFileChunkGap(std::uint64_t download_chunk_standard_size, std::uint64_t file_chunk_standard_size,
                          std::size_t download_chunk_index, const std::vector<S3URIFileChunkGapSpec> &file_chunk_gaps)
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
        for (const auto &file_chunk_gap : file_chunk_gaps)
        {
            const bool pass = std::visit(
                [&download_chunk_start_byte, download_chunk_end_byte, file_chunk_standard_size](const auto &gap_spec)
                {
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