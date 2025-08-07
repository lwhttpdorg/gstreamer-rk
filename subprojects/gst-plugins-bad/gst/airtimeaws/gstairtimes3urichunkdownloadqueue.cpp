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
#include <utility>

namespace gst::airtime
{

S3URIChunkDownloadQueue::S3URIChunkDownloadQueue(std::uint64_t total_size, std::uint64_t chunk_size,
                                                 DownloadChunkFilter download_chunk_filter)
{
    // Initialize the queue with chunks based on the total size and chunk size
    std::size_t index = 0;
    for (std::uint64_t start_byte = 0; start_byte < total_size; start_byte += chunk_size)
    {
        const std::uint64_t chunk_size_adjusted = std::min(chunk_size, total_size - start_byte);
        S3URIChunkSpec chunk_spec{index, start_byte, chunk_size_adjusted, chunk_size};
        if (download_chunk_filter(chunk_spec))
        {
            // Only add the chunk if it passes the filter
            queue_.emplace_back(std::move(chunk_spec));
        }
        ++index;
    }
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
        // Check if the chunk overlaps with the given byte range
        if (((chunk.startByte() >= byte_offset) and (chunk.startByte() < byte_offset + size)) or
            ((chunk.endByte() >= byte_offset) and (chunk.endByte() < byte_offset + size)) or
            ((chunk.startByte() < byte_offset and chunk.endByte() >= byte_offset + size)))
        {
            // Set the priority of the chunk to the highest
            chunk.priority(it->priority() + 1);
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

} // namespace gst::airtime