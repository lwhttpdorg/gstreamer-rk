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

#include <cassert>
#include <cstdint>

#include "gstairtimes3srccontext.h"
#include "gstairtimescopedresource.hpp"

#include <glib-object.h>
#include <gst/gst.h>
#include <json-glib/json-glib.h>

namespace gst::airtime
{

namespace details
{

template <typename T, auto Fun>
void AddressOfPtr(T* rsc)
{
    Fun(&rsc);
}

} // namespace details

/// @brief Scoped resource management for GObject and GStreamer types.
using ScopedGChar = ResourceReleasedByFunction<gchar, g_free>;
using ScopedGError = ResourceReleasedByFunction<GError, details::AddressOfPtr<GError, g_clear_error>>;
using ScopedGstAirtimeS3SrcContext = ResourceReleasedByFunction<GstAirtimeS3SrcContext, g_object_unref>;
using ScopedGstMessage = ResourceReleasedByFunction<GstMessage, gst_message_unref>;
using ScopedGstQuery = ResourceReleasedByFunction<GstQuery, gst_query_unref>;
using ScopedGstStructure = ResourceReleasedByFunction<GstStructure, gst_structure_free>;
using ScopedGstURI = ResourceReleasedByFunction<GstUri, gst_uri_unref>;
using ScopedJsonBuilder = ResourceReleasedByFunction<JsonBuilder, g_object_unref>;
using ScopedJsonParser = ResourceReleasedByFunction<JsonParser, g_object_unref>;
using ScopedJsonGenerator = ResourceReleasedByFunction<JsonGenerator, g_object_unref>;
using ScopedJsonNode = ResourceReleasedByFunction<JsonNode, json_node_unref>;

/// @brief Simple RAII wrapper for GstBuffer mapped memory.
class MappedBuffer
{
public:
    explicit MappedBuffer(GstBuffer* buffer, GstMapFlags flags = GST_MAP_READ) noexcept :
        buffer_{buffer},
        mapped_{gst_buffer_map(buffer_, &map_info_, flags) != 0}
    {
        assert(buffer_);
    }
    MappedBuffer(const MappedBuffer&) = delete;
    MappedBuffer(MappedBuffer&&) = delete;
    MappedBuffer& operator=(MappedBuffer&) = delete;
    MappedBuffer& operator=(MappedBuffer&&) = delete;

    ~MappedBuffer() noexcept
    {
        if (mapped_)
        {
            gst_buffer_unmap(buffer_, &map_info_);
        }
    }

    explicit operator bool() const noexcept
    {
        return mapped_;
    }

    const std::uint8_t* data() const noexcept
    {
        return mapped_ ? map_info_.data : nullptr;
    }

    std::uint8_t* data() noexcept
    {
        return mapped_ ? map_info_.data : nullptr;
    }

    std::size_t size() const noexcept
    {
        return mapped_ ? map_info_.size : 0;
    }

private:
    GstBuffer* buffer_ = nullptr;
    GstMapInfo map_info_ = GST_MAP_INFO_INIT;
    bool mapped_ = false;
};

/// @brief Creates a mapped read buffer for the specified GstBuffer.
/// @param buffer The GstBuffer to map.
/// @return A MappedBuffer for reading from the specified GstBuffer.
inline MappedBuffer getMappedReadBuffer(GstBuffer* buffer) noexcept
{
    return MappedBuffer{buffer, GST_MAP_READ};
}

/// @brief Creates a mapped write buffer for the specified GstBuffer.
/// @param buffer The GstBuffer to map.
/// @return A MappedBuffer for writing to the specified GstBuffer.
inline MappedBuffer getMappedWriteBuffer(GstBuffer* buffer) noexcept
{
    return MappedBuffer{buffer, GST_MAP_WRITE};
}

/// @brief Creates a mapped read-write buffer for the specified GstBuffer.
/// @param buffer The GstBuffer to map.
/// @return A MappedBuffer for reading and writing to the specified GstBuffer.
inline MappedBuffer getMappedReadWriteBuffer(GstBuffer* buffer) noexcept
{
    return MappedBuffer{buffer, static_cast<GstMapFlags>(GST_MAP_READ | GST_MAP_WRITE)};
}

} // namespace gst::airtime