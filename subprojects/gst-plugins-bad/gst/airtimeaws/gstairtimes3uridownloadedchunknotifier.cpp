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

#include "gstairtimes3uridownloadedchunknotifier.hpp"

#include <cassert>

namespace gst::airtime
{

bool S3URIDownloadedChunkNotifier::waitForRangeAvailable(std::uint64_t byte_offset, std::uint64_t size,
                                                         std::chrono::seconds timeout)
{
    assert(size > 0);

    std::unique_lock lock{access_};
    if (timeout == std::chrono::seconds::zero())
    {
        // Wait indefinitely until the range is available or interrupted
        range_available_cond_.wait(lock, [this, byte_offset, size] {
            if (interrupt_flag_)
            {
                return true; // If interrupted, return true to exit the wait
            }
            return isRangeAvailable(byte_offset, size);
        });
    }
    else
    {
        // Wait for the specified timeout duration or interruption
        if (not range_available_cond_.wait_for(lock, timeout, [this, byte_offset, size] {
                if (interrupt_flag_)
                {
                    return true; // If interrupted, return true to exit the wait
                }
                return isRangeAvailable(byte_offset, size);
            }))
        {
            return false; // Timeout occurred without the range being available
        }
    }
    return not interrupt_flag_;
}

void S3URIDownloadedChunkNotifier::stopWaitingForRange()
{
    std::lock_guard lock{access_};
    interrupt_flag_ = true;
    // multiple threads might be waiting for the same S3 object range or for different but overlapping ranges
    range_available_cond_.notify_all();
}

void S3URIDownloadedChunkNotifier::notifyChunkDownloaded(std::uint64_t start_byte, std::uint64_t size)
{
    std::lock_guard lock{access_};
    downloaded_chunks_.emplace_back(start_byte, start_byte + size - 1); // Store the range as (start_byte, end_byte)
    // multiple threads might be waiting for the same S3 object range or for different but overlapping ranges
    range_available_cond_.notify_all();
}

bool S3URIDownloadedChunkNotifier::isRangeAvailable(std::uint64_t byte_offset, std::uint64_t size) const
{
    const std::uint64_t requested_end_byte = byte_offset + size - 1;
    // iterate over the downloaded ranges and check if the requested range is fully covered
    std::uint64_t covered_size = 0;
    for (auto& [start_byte, end_byte] : downloaded_chunks_)
    {
        // Check if the chunk overlaps with the requested byte range. It overlaps if, either:
        // 1. The start byte of the chunk is within the requested range.
        if ((start_byte >= byte_offset) and (start_byte < byte_offset + size))
        {
            const auto end_byte_adj = std::min(end_byte, requested_end_byte);
            covered_size += (end_byte_adj - start_byte + 1);
        }

        // 2. The end byte of the chunk is within the requested range.
        else if ((end_byte >= byte_offset) and (end_byte < byte_offset + size))
        {
            covered_size += (end_byte - byte_offset + 1);
        }

        // 3. The chunk fully covers the requested range (start byte is before the requested range and end byte is
        //    after the end of the requested range).
        else if ((start_byte < byte_offset and end_byte >= byte_offset + size))
        {
            return true; // The chunk fully covers the requested range
        }
    }
    return covered_size >= size;
}

} // namespace gst::airtime