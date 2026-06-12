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

#include "gstairtimes3uriproviders.hpp"

#include <memory>
#include <mutex>
#include <utility>

namespace gst::airtime
{

/// @brief Factory class for creating and managing S3 URI providers. It guarantees that at most one instance of a
/// S3URIProviders is active at a time.
class S3URIProvidersFactory
{
private:
    S3URIProvidersFactory() = default;

public:
    ~S3URIProvidersFactory() = default;

    /// @brief Create a new instance of the cache, or return the existing instance if it already
    /// exists. This will create the cache directory and the purged cache directory.
    /// @param config The configuration for the cache.
    /// @return A pair containing a shared pointer to the cache instance, and a boolean indicating whether a new
    /// instance was created (true) or an existing instance was returned (false).
    static std::pair<std::shared_ptr<S3URIProviders>, bool> create(S3URIProviderConfig config);

    /// @brief Get the existing instance of the cache, or null if it does not exist. This will not create a new
    /// instance, but will return the existing one if it exists.
    /// @return A shared pointer to the cache instance, or nullptr if it does not exist.
    static std::shared_ptr<S3URIProviders> get();

private:
    static std::weak_ptr<S3URIProviders> cached_instance_;

    static std::mutex instance_access_;
};

} // namespace gst::airtime