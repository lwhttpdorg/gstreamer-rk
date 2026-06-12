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
#include <functional>
#include <mutex>
#include <optional>
#include <vector>

#include "gstairtimes3urichunkspec.hpp"

namespace gst::airtime
{

/// @brief A thread-safe queue for managing download chunks with priority handling.
/// It allows adding chunks, retrieving the next chunk with the highest priority, and setting priorities for specific
/// byte ranges.
class S3URIChunkDownloadQueue
{
public:
    /// @brief A filter function to determine if a download chunk should be included in the queue.
    /// @param download_chunk_spec The specification of the download chunk.
    /// @return True if the chunk should be included, false otherwise.
    /// @note This filter can be used to skip certain chunks based on custom logic, for instance to accept only these
    /// chunks which are missing in cache.
    using DownloadChunkFilter = std::function<bool(const S3URIChunkSpec& download_chunk_spec)>;

    /// @param total_size The total size of the data to be downloaded.
    /// @param chunk_size The size of each download chunk.
    /// @param download_chunk_filter Filter to reject any unwanted chunks. By default a passthrough filter is used.
    /// @note The queue is initialized with chunks based on the total size and chunk size.
    S3URIChunkDownloadQueue(
        std::uint64_t total_size, std::uint64_t chunk_size,
        DownloadChunkFilter download_chunk_filter = [](const S3URIChunkSpec&) { return true; });

    /// @brief Adds a new chunk to the queue.
    /// @param chunk The S3URIChunkSpec object representing the chunk to add.
    /// @note This method is thread-safe and can be called from multiple threads.
    void addChunk(S3URIChunkSpec chunk);

    /// @brief Adds a new chunk to the queue with the highest priority.
    /// @param chunk The S3URIChunkSpec object representing the chunk to add.
    /// @note This method is thread-safe and can be called from multiple threads.
    void addChunkWithHighestPriority(S3URIChunkSpec chunk);

    /// @brief Sets the priority of all chunks that overlap with the specified byte range to the highest priority.
    /// @param byte_offset The starting byte offset of the range to prioritize.
    /// @param size The size of the range to prioritize.
    /// @note This method is thread-safe and can be called from multiple threads.
    void setHighestPriorityForRange(std::uint64_t byte_offset, std::uint64_t size);

    /// @brief Makes all chunks that overlap with the specified byte range to be consumed next once consuming of all
    /// already prioritized chunks is complete.
    /// @param byte_offset The starting byte offset of the range to prioritize.
    /// @param size The size of the range to prioritize.
    /// @note This method is thread-safe and can be called from multiple threads.
    void prioritizeRange(std::uint64_t byte_offset, std::uint64_t size);

    /// @brief Returns the number of chunks in the queue.
    /// @return The number of chunks in the queue.
    /// @note This method is thread-safe and can be called from multiple threads.
    std::size_t size() const;

    /// @brief Checks if the queue is empty.
    /// @return True if the queue is empty, false otherwise.
    /// @note This method is thread-safe and can be called from multiple threads.
    bool empty() const;

    /// @brief Retrieves the next chunk from the queue with the highest priority and removes it from the queue.
    /// @note If no chunks are available, it returns std::nullopt.
    /// @note This method is thread-safe and can be called from multiple threads.
    /// @return An optional S3URIChunkSpec object representing the next chunk with the highest priority, or
    /// std::nullopt if no chunks are available.
    /// @note The chunk is removed from the queue after retrieval.
    /// @note If multiple chunks have the same highest priority, the one that was added first will be returned.
    std::optional<S3URIChunkSpec> getNextChunk();

private:
    /// @brief Finds the chunk with the highest priority in the queue.
    /// @return An iterator to the chunk with the highest priority, or queue_.end() if the queue is empty.
    std::vector<S3URIChunkSpec>::const_iterator findHighestPriorityChunk() const;

    mutable std::mutex queue_access_;
    std::vector<S3URIChunkSpec> queue_;
};

/// @brief Check if two ranges overlap.
/// @param from1 Start of the first range
/// @param to1 End of the first range
/// @param from2 Start of the second range
/// @param to2 End of the second range
/// @return true if the ranges overlap, false otherwise
bool areRangesOverlapping(std::uint64_t from1, std::uint64_t to1, std::uint64_t from2, std::uint64_t to2) noexcept;

} // namespace gst::airtime