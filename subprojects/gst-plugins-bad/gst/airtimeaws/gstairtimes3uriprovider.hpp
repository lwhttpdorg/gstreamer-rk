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

#include "gstairtimes3bufferfillstatus.hpp"
#include "gstairtimes3urichunkdownloadqueue.hpp"
#include "gstairtimes3urichunkprocessor.hpp"
#include "gstairtimes3urichunksource.hpp"
#include "gstairtimes3urichunkspec.hpp"
#include "gstairtimes3uridownloadedchunknotifier.hpp"
#include "gstairtimes3uriproviderconfig.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>

namespace gst::airtime
{

/// @brief S3 URI provider for managing S3 object access and chunk processing.
/// A typical use case in GStreamer is to call the getContentLength() and then fill() method to fill a buffer with data
/// from the S3 object.
class S3URIProvider
{
public:
    /// @brief Constructor may throw an exception if the S3 object cannot be accessed or if the configuration is
    /// invalid. It does a couple of steps, in particular:
    /// 1) Checks and ensures that the S3 object exists and is accessible.
    /// 2) Retrieves the S3 object metadata, including the content length and entity tag
    /// 3) Checks with chunk processor based on the metadata if the chunks are needed or not. If not, it means that they
    /// are already available in the processor object.
    /// All of this operations may throw an exception.
    /// @param s3_bucket The S3 bucket name.
    /// @param s3_key The S3 object key.
    /// @param config The S3 URI provider configuration.
    /// @param chunk_source The chunk source to use for downloading chunks.
    /// @param chunk_processor The chunk processor to use for processing downloaded chunks.
    S3URIProvider(std::string_view s3_bucket, std::string_view s3_key, S3URIProviderConfig config,
                  std::unique_ptr<S3URIChunkSource> chunk_source, std::unique_ptr<S3URIChunkProcessor> chunk_processor);
    ~S3URIProvider();

    /// @brief Check if the provider has an error coming from underlying async operation.
    /// @note This method is thread-safe and can be called from multiple threads.
    /// @return True if the provider has an error, false otherwise.
    bool hasError() const;

    enum class ErrorType {
        download,  ///< Error in downloading chunks
        processor, ///< Error in processing chunks (usually cache related)
        other      ///< Other error
    };

    /// @brief Get the error type if the provider is in an error state. The error comes from underlying async operation.
    /// @note This method is thread-safe and can be called from multiple threads.
    /// @return The error type if there is an error, or an empty optional if there is no error.
    std::optional<ErrorType> getErrorType() const;

    /// @brief Get the error message if the provider is in an error state. The error comes from underlying async
    /// operation. If the provider is not in an error state, this will return an empty optional.
    /// @note This method is thread-safe and can be called from multiple threads.
    /// @return An optional containing the error message if there is an error, or an empty optional if there is no
    /// error.
    std::optional<std::string> getLastErrorMessage() const;

    /// @brief Get the content length of the S3 object.
    /// @return The content length of the S3 object.
    std::uint64_t getContentLength() const;

    /// @brief Wait synchronously for all chunks of the S3 object to be available. It blocks the calling thread as long
    /// as the provider is still fetching the data.
    /// @param timeout The maximum time to wait for all chunks to be available. If zero, wait indefinitely.
    /// @return A boolean indicating success or failure.
    /// @note If the provider is in an error state, this method will throw an exception.
    bool waitForAllChunksDownloaded(std::chrono::seconds timeout = std::chrono::seconds::zero());

    /// @brief Wait synchronously for a specific byte range of the S3 object to be available. It blocks the calling
    /// thread as long as the provider is still fetching the requested range of data.
    /// @param offset The starting byte offset of the range to wait for.
    /// @param size The size of the range to wait for.
    /// @param timeout The maximum time to wait for the range to be available. If zero, wait indefinitely.
    /// @return A boolean indicating success or failure.
    /// @note If the provider is in an error state, this method will throw an exception.
    bool waitForRangeDownloaded(std::uint64_t offset, std::uint64_t size,
                                std::chrono::seconds timeout = std::chrono::seconds{0});

    /// @brief Fill the provided buffer with data from the S3 object. If the URI provider is still fetching the
    /// data, the chunk corresponding to the offset will be requested and prioritized for download blocking the thread
    /// calling this method. As soon as the data is available, it will be copied to the provided buffer and the thread
    /// will be unblocked.
    /// @param data The buffer to fill.
    /// @param offset The offset to start filling from.
    /// @param size The size to fill.
    /// @param timeout The maximum time to wait for the data to become available. If zero, wait indefinitely.
    /// @return A pair containing the fill status and the number of bytes read.
    /// @note If the provider is in an error state, this method will throw an exception.
    std::pair<S3BufferFillStatus, std::uint64_t> fill(std::uint8_t* data, std::uint64_t offset, std::uint64_t size,
                                                      std::chrono::seconds timeout = std::chrono::seconds{0});

    enum class DownloadCompletionState {
        in_progress,      ///< The download is in progress.
        fully_downloaded, ///< Downloaded completely from S3 (nothing from cache)
        partially_cached, ///< Some chunks from cache, some downloaded
        fully_cached,     ///< All chunks from cache (no download needed)
        download_failed   ///< Failed to download or restore (error state)
    };

    /// @brief Gets the download completion state of the S3 object.
    /// @return The download completion state.
    DownloadCompletionState getDownloadCompletionState() const;

    /// @brief Gets the download completion state as a null-terminated string.
    /// @return A null-terminated string view representing the download completion state.
    std::string_view getDownloadCompletionStateString() const noexcept;

    /// @brief Gets the metrics of the provider: the duration in nanoseconds (the total time taken to
    /// download the object from S3).
    /// @return The duration in nanoseconds.
    /// @note We could expand this in the future with additional metrics.
    std::uint64_t getMetrics() const;

private:
    /// @brief Performs all actions necessary to make the S3 object available locally.
    /// @note This is an asynchronous operation.
    void getS3URIObjectAsync();

    /// @brief Gets the content length of the S3 object. This is used to determine the number of chunks to fetch.
    /// @note This method will throw an exception if the content length cannot be retrieved.
    void processContentLength();

    /// @brief Starts downloading the S3 object in chunks.
    void startDownloadingS3ObjectChunks(const std::vector<S3URIFileChunkGapSpec>& file_chunk_gaps);

    /// @brief Initiates an asynchronous download of a chunk from S3.
    /// @param chunk_spec The specification of the chunk to download, including the byte range and priority.
    /// @param retry_count The number of retry attempts to download the chunk.
    void initiateAsyncDownload(S3URIChunkSpec chunk_spec, unsigned retry_count);

    /// @brief Cancels all active async requests downloading chunks.
    void cancelAsyncDownload();

    /// @brief Sets the error status of the provider. This will prevent further operations from being performed.
    /// @param type The type of error that occurred.
    /// @param message The error message to set.
    /// @note This method is thread-safe and can be called from multiple threads.
    void setError(ErrorType type, std::string message);

private:
    S3URIProviderConfig config_;

    std::string s3_bucket_;
    std::string s3_key_;
    std::chrono::time_point<std::chrono::steady_clock> fetch_start_time_ = std::chrono::steady_clock::now();
    std::unique_ptr<S3URIChunkSource> chunk_source_;
    std::unique_ptr<S3URIChunkProcessor> chunk_processor_;
    std::unique_ptr<S3URIDownloadedChunkNotifier> downloaded_chunk_notifier_;
    std::unique_ptr<S3URIChunkDownloadQueue> download_chunk_queue_;
    S3URIObjectMetadata s3_object_metadata_;

    std::atomic<DownloadCompletionState> download_completion_state_{DownloadCompletionState::in_progress};
    std::atomic_uint64_t fetch_duration_{0};
    std::atomic<int> active_requests_count_{0};

    mutable std::mutex error_access_;
    std::optional<ErrorType> error_type_;
    std::string last_error_message_;
};

/// @brief Creates a cache eviction policy for the S3 URI provider.
/// @param config The configuration for the S3 URI provider.
/// @return A unique pointer to the created cache eviction policy.
std::unique_ptr<S3URICacheEvictionPolicy> createEvictionPolicy(const S3URIProviderConfig& config);

/// @brief Creates a chunk source for the S3 URI provider.
/// @param s3_bucket The S3 bucket to use.
/// @param s3_key The S3 key to use.
/// @param config The configuration for the S3 URI provider.
/// @return A unique pointer to the created chunk source.
std::unique_ptr<S3URIChunkSource> createChunkSource(std::string_view s3_bucket, std::string_view s3_key,
                                                    const S3URIProviderConfig& config);

/// @brief Creates an S3 URI provider.
/// @param cache_manager The cache manager to use.
/// @param s3_bucket The S3 bucket to use.
/// @param s3_key The S3 key to use.
/// @param config The configuration for the S3 URI provider.
/// @return A shared pointer to the created S3 URI provider.
std::shared_ptr<S3URIProvider> createS3URIProvider(std::shared_ptr<S3URICacheManager> cache_manager,
                                                   std::string_view s3_bucket, std::string_view s3_key,
                                                   const S3URIProviderConfig& config);

/// @brief Converts the download completion state to a string.
/// @param state The download completion state to convert.
/// @return A null-terminated string view representing the download completion state.
std::string_view downloadCompletionStateToString(S3URIProvider::DownloadCompletionState state) noexcept;

/// @brief Passes only file chunks missing in the cache.
/// @param download_chunk_standard_size The standard size of the download chunks.
/// @param file_chunk_standard_size The standard size of the file chunks.
/// @param download_chunk_index The index of the download chunk.
/// @param file_chunk_gaps The file chunk gaps to check against.
/// @note This function is used to filter out download chunks that are not needed based on the file chunk gaps.
/// @return True if the chunk should be included, false otherwise.
bool passFileChunkGap(std::uint64_t download_chunk_standard_size, std::uint64_t file_chunk_standard_size,
                      std::size_t download_chunk_index, const std::vector<S3URIFileChunkGapSpec>& file_chunk_gaps);

} // namespace gst::airtime