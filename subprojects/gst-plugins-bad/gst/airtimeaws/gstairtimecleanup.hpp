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
#include <new>
#include <type_traits>
#include <utility>

namespace gst::airtime
{

namespace details
{

template <typename Callback>
constexpr bool returnsVoid()
{
    return std::is_same<std::invoke_result_t<Callback>, void>::value;
}

template <typename Callback>
class Storage
{
public:
    Storage() = delete;

    explicit Storage(Callback callback)
    {
        // Placement-new into a character buffer is used for eager destruction when
        // the cleanup is invoked or cancelled.
        ::new (getCallbackBuffer()) Callback(std::move(callback));
        is_callback_engaged_ = true;
    }

    Storage(Storage&& other)
    {
        assert(other.isCallbackEngaged());
        ::new (getCallbackBuffer()) Callback(std::move(other.getCallback()));
        is_callback_engaged_ = true;
        other.destroyCallback();
    }

    Storage(const Storage& other) = delete;
    Storage& operator=(Storage&& other) = delete;
    Storage& operator=(const Storage& other) = delete;

    void* getCallbackBuffer() noexcept
    {
        return static_cast<void*>(+callback_buffer_);
    }

    Callback& getCallback() noexcept
    {
        return *reinterpret_cast<Callback*>(getCallbackBuffer());
    }

    bool isCallbackEngaged() const noexcept
    {
        return is_callback_engaged_;
    }

    void destroyCallback()
    {
        is_callback_engaged_ = false;
        getCallback().~Callback();
    }

    void invokeCallback()
    {
        std::move(getCallback())();
    }

private:
    bool is_callback_engaged_;
    alignas(Callback) char callback_buffer_[sizeof(Callback)];
};

} // namespace details

/// @brief It is a control-flow-construct-like type which is used for executing a provided callback on scope exit.
/// Simplifies managing the resources by tying their lifetimes to the scope in which they are used.
/// Simplifies also resource cleanup in the presence of exceptions.
/// An example use case:
///
///     Cleanup rm_dir = [path] { std::filesystem::remove_all(path); }; // calls callback in dtor
///     // ...
///     if(error)
///     {
///         throw std::runtime_error("Unexpected error");
///     }
///
template <typename Callback = void()>
class Cleanup final
{
    static_assert(details::returnsVoid<Callback>(), "Callbacks that return values are not supported.");

public:
    Cleanup(Callback callback) :
        storage_(std::move(callback))
    {
    }

    Cleanup(Cleanup&& other) = default;

    ~Cleanup()
    {
        if (storage_.isCallbackEngaged())
        {
            storage_.invokeCallback();
            storage_.destroyCallback();
        }
    }

    /// @brief Cancel calling the cleanup callback.
    /// Example:
    ///     Cleanup rm_dir = [path] { std::filesystem::remove_all(path); };
    ///     std::move(rm_dir).cancel();
    void cancel() &&
    {
        assert(storage_.isCallbackEngaged());
        storage_.destroyCallback();
    }

    /// @brief Force-calls the cleanup callback instead of calling it on scope exit.
    /// Example:
    ///     Cleanup rm_dir = [path] { std::filesystem::remove_all(path); };
    ///     std::move(rm_dir).invoke();
    void invoke() &&
    {
        assert(storage_.isCallbackEngaged());
        storage_.invokeCallback();
        storage_.destroyCallback();
    }

private:
    details::Storage<Callback> storage_;
};

// `Cleanup c = /* callback */;`
// C++17 type deduction API for creating an instance of `Cleanup`
template <typename Callback>
Cleanup(Callback callback) -> Cleanup<Callback>;

} // namespace gst::airtime