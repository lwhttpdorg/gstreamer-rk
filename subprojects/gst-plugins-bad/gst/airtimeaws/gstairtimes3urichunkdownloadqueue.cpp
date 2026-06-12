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

#include "gstairtimes3urichunkdownloadqueue.hpp"

#include <algorithm>
#include <cassert>
#include <utility>

namespace
{

// Helper functions for chunk calculations. We use these to determine how to initialize the queue.
// GStreamer when operating on a file tends to need the beginning and end of the file first (we call each that part a
// segment in the functions below). We prioritize chunks accordingly, i.e. add beginning chunks first - as much as
// needed to cover the beginning segment, then ending chunks - again, as much as needed to cover the end segment, and
// finally the middle part.

/// @brief Calculate the number of chunks needed to download a file.
/// @param total_size The total size in bytes of the file to be downloaded.
/// @param chunk_size The size in bytes of each chunk.
/// @return The number of chunks needed to download the file.
auto calcNumOfChunks(std::uint64_t total_size, std::uint64_t chunk_size) noexcept
{
    return (total_size + chunk_size - 1) / chunk_size;
}

/// @brief Calculate the number of chunks needed to cover a segment of N bytes.
/// @param chunk_size The size in bytes of each chunk.
/// @param segment_size The size in bytes of the segment.
/// @return The number of chunks needed to cover the segment of N bytes.
auto calcNumOfChunksForSegment(std::uint64_t chunk_size, std::uint64_t segment_size) noexcept
{
    return (segment_size + chunk_size - 1) / chunk_size;
}

/// @brief Calculates the beginning index of the ending range of chunks to prioritize.
/// @param total_size The total size in bytes of the file to be downloaded.
/// @param chunk_size The size in bytes of each chunk.
/// @param num_of_chunks_per_segment The number of chunks that fit in the segment size.
/// @return The beginning index of the ending range of chunks to prioritize.
auto calcEngRangeBegIndex(std::uint64_t total_size, std::uint64_t chunk_size,
                          std::uint64_t num_of_chunks_per_segment) noexcept
{
    const auto num_chunks = calcNumOfChunks(total_size, chunk_size);
    const bool last_chunk_is_full_size = (total_size % chunk_size == 0);

    if (last_chunk_is_full_size)
    {
        return (num_chunks > num_of_chunks_per_segment) ? (num_chunks - num_of_chunks_per_segment) : num_chunks;
    }
    else
    {
        return (num_chunks > num_of_chunks_per_segment + 1) ? (num_chunks - num_of_chunks_per_segment - 1) : num_chunks;
    }
}

} // namespace

namespace gst::airtime
{

S3URIChunkDownloadQueue::S3URIChunkDownloadQueue(std::uint64_t total_size, std::uint64_t chunk_size,
                                                 DownloadChunkFilter download_chunk_filter)
{
    // Initialize the queue with chunks based on the total size and chunk size
    const auto num_chunks = calcNumOfChunks(total_size, chunk_size);
    queue_.reserve(num_chunks);

    const auto add_chunk = [&](std::uint64_t index, std::uint64_t start_byte, std::uint64_t actual_size) {
        S3URIChunkSpec chunk_spec{index, start_byte, actual_size, chunk_size};
        if (download_chunk_filter(chunk_spec))
        {
            // Only add the chunk if it passes the filter
            queue_.emplace_back(std::move(chunk_spec));
        }
    };

    const auto add_range_chunks = [&](std::uint64_t start_index, std::uint64_t end_index) {
        for (std::uint64_t i = start_index; i < end_index; ++i)
        {
            const std::uint64_t start_byte = i * chunk_size;
            const std::uint64_t chunk_size_adjusted = std::min(chunk_size, total_size - start_byte);
            add_chunk(i, start_byte, chunk_size_adjusted);
        }
    };

    constexpr std::uint64_t segment_2MB = 2 * 1024 * 1024;
    const std::uint64_t num_chunks_for_2MB_segment = calcNumOfChunksForSegment(chunk_size, segment_2MB);

    const std::uint64_t beg_range_beg_index = 0;
    const std::uint64_t beg_range_end_index = std::min(num_chunks, num_chunks_for_2MB_segment);

    const std::uint64_t end_range_beg_index =
        std::max(calcEngRangeBegIndex(total_size, chunk_size, num_chunks_for_2MB_segment), beg_range_end_index);
    const std::uint64_t end_range_end_index = num_chunks;

    // middle range is what is left
    const std::uint64_t middle_range_beg_index = beg_range_end_index;
    const std::uint64_t middle_range_end_index = end_range_beg_index;

    add_range_chunks(beg_range_beg_index, beg_range_end_index);
    add_range_chunks(end_range_beg_index, end_range_end_index);
    add_range_chunks(middle_range_beg_index, middle_range_end_index);
}

void S3URIChunkDownloadQueue::addChunk(S3URIChunkSpec chunk)
{
    std::lock_guard lock{queue_access_};
    queue_.emplace_back(std::move(chunk));
}

void S3URIChunkDownloadQueue::addChunkWithHighestPriority(S3URIChunkSpec chunk)
{
    std::lock_guard lock{queue_access_};
    auto it = findHighestPriorityChunk();
    if (it == queue_.end())
    {
        queue_.push_back(std::move(chunk));
    }
    else
    {
        // Insert the new chunk before the one with the highest priority
        chunk.priority(it->priority() + 1); // Increase priority of the new chunk
        queue_.emplace_back(std::move(chunk));
    }
}

void S3URIChunkDownloadQueue::setHighestPriorityForRange(std::uint64_t byte_offset, std::uint64_t size)
{
    std::lock_guard lock{queue_access_};
    auto it = findHighestPriorityChunk();
    if (it == queue_.end())
    {
        // No chunks in the queue, nothing to prioritize
        return;
    }

    for (auto& chunk : queue_)
    {
        if (areRangesOverlapping(chunk.startByte(), chunk.endByte(), byte_offset, byte_offset + size - 1))
        {
            // Set the priority of the chunk to the highest
            chunk.priority(it->priority() + 1);
        }
    }
}

void S3URIChunkDownloadQueue::prioritizeRange(std::uint64_t byte_offset, std::uint64_t size)
{
    std::lock_guard lock{queue_access_};

    for (auto& chunk : queue_)
    {
        // 1) For non-prioritized chunks that overlaps we increase their priority to 1.
        // 2) For already prioritized chunks we bump their priority by 1 to make them still more important not to starve
        // the consumers waiting for them.
        if (areRangesOverlapping(chunk.startByte(), chunk.endByte(), byte_offset, byte_offset + size - 1) or
            chunk.priority() > 0)
        {
            chunk.increasePriority();
        }
    }
}

std::size_t S3URIChunkDownloadQueue::size() const
{
    std::lock_guard lock{queue_access_};
    return queue_.size();
}

bool S3URIChunkDownloadQueue::empty() const
{
    std::lock_guard lock{queue_access_};
    return queue_.empty();
}

std::optional<S3URIChunkSpec> S3URIChunkDownloadQueue::getNextChunk()
{
    std::lock_guard lock{queue_access_};
    auto it = findHighestPriorityChunk();
    if (it == queue_.end())
    {
        return std::nullopt;
    }

    S3URIChunkSpec chunk = *it;
    queue_.erase(it);
    return chunk;
}

std::vector<S3URIChunkSpec>::const_iterator S3URIChunkDownloadQueue::findHighestPriorityChunk() const
{
    return std::max_element(queue_.begin(), queue_.end(), [](const S3URIChunkSpec& a, const S3URIChunkSpec& b) {
        return a.priority() < b.priority();
    });
}

bool areRangesOverlapping(std::uint64_t from1, std::uint64_t to1, std::uint64_t from2, std::uint64_t to2) noexcept
{
    assert(from1 <= to1);
    assert(from2 <= to2);
    // Two ranges overlap if one range doesn't end before the other starts
    return from1 <= to2 and from2 <= to1;
}

} // namespace gst::airtime