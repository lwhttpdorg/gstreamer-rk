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

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

namespace gst::airtime
{

/// @brief Notifies when a downloaded chunk becomes available for a specific byte range.
/// This class allows waiting for a specific byte range to become available after a chunk is downloaded.
class S3URIDownloadedChunkNotifier
{
public:
    /// @brief Wait for a specific byte range to become available.
    /// @param byte_offset The starting byte offset of the range to wait for.
    /// @param size The size of the range to wait for.
    /// @param timeout The maximum time to wait for the range to become available. If zero, wait indefinitely.
    /// @return True if the range is available, false if the wait timed out or was interrupted.
    /// @note This method is thread-safe and can be called from multiple threads.
    /// @note If the wait is interrupted by calling `stopWaitingForRange`, the method will return false immediately.
    bool waitForRangeAvailable(std::uint64_t byte_offset, std::uint64_t size,
                               std::chrono::seconds timeout = std::chrono::seconds::zero());

    /// @brief Stop waiting for a range to become available.
    /// @note This will interrupt any ongoing wait and notify the condition variable to wake up any waiting threads.
    /// This is useful when you want to cancel waiting for a specific range, for example, when the download is no longer
    /// needed. After calling this, any subsequent calls to `waitForRangeAvailable` will return false immediately.
    /// @note This method is thread-safe.
    void stopWaitingForRange();

    /// @brief Called when a chunk is downloaded.
    /// @param start_byte The starting byte of the downloaded chunk.
    /// @param size The size of the downloaded chunk.
    /// @note This method is thread-safe and will notify any waiting threads that a chunk has been downloaded.
    void notifyChunkDownloaded(std::uint64_t start_byte, std::uint64_t size);

private:
    bool isRangeAvailable(std::uint64_t byte_offset, std::uint64_t size) const;

    std::mutex access_;
    std::condition_variable range_available_cond_;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> downloaded_chunks_; // (start_byte, end_byte) pairs
    bool interrupt_flag_ = false;
};

} // namespace gst::airtime