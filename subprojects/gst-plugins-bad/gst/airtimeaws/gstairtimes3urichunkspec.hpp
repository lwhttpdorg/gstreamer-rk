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

#include <cstddef>
#include <cstdint>

namespace gst::airtime
{

/// @brief Describes an URI chunk with its index, start byte, size, and priority.
class S3URIChunkSpec
{
public:
    /// @param index Index of the chunk, the URI is divided into.
    /// @param start_byte Starting byte of the URI's chunk.
    /// @param actual_size Size of the chunk in bytes. This is the actual size of the data that will be downloaded. It
    /// may be smaller than the standard size if the last chunk is smaller than the standard size.
    /// @param standard_size Size of the chunk in bytes. This is the size the total data is divided into.
    /// @param priority Priority of the chunk, higher means higher priority. Default is 0 which means unprioritized
    /// chunk.
    /// @note The priority can be used to prioritize chunks for download.
    S3URIChunkSpec(std::size_t index, std::uint64_t start_byte, std::uint64_t actual_size, std::uint64_t standard_size,
                   int priority = 0) :
        index_{index},
        start_byte_{start_byte},
        actual_size_{actual_size},
        standard_size_{standard_size},
        priority_{priority}
    {
    }

    std::size_t index() const noexcept
    {
        return index_;
    }
    std::uint64_t startByte() const noexcept
    {
        return start_byte_;
    }
    std::uint64_t endByte() const noexcept
    {
        return start_byte_ + actual_size_ - 1;
    }
    std::uint64_t actualSize() const noexcept
    {
        return actual_size_;
    }
    std::uint64_t standardSize() const noexcept
    {
        return standard_size_;
    }
    int priority() const noexcept
    {
        return priority_;
    }
    void priority(int priority) noexcept
    {
        priority_ = priority;
    }
    void increasePriority(int delta = 1) noexcept
    {
        priority_ += delta;
    }

private:
    std::size_t index_{0}; // Index of the download chunk
    std::uint64_t start_byte_{0};
    std::uint64_t actual_size_{0};
    std::uint64_t standard_size_{0};
    int priority_{0};
};

} // namespace gst::airtime