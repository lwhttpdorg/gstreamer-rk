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

#include <aws/core/Aws.h>

#include <mutex>

namespace gst::airtime
{

    /// @brief Simple RAII wrapper on AWS SDK initialization and shutdown.
    class AwsEnv
    {
    public:
        AwsEnv()
        {
            options_.httpOptions.installSigPipeHandler = true;
            Aws::InitAPI(options_);
        }
        AwsEnv(const AwsEnv &) = delete;
        AwsEnv(AwsEnv &&) = delete;
        AwsEnv &operator=(const AwsEnv &) = delete;
        AwsEnv &operator=(AwsEnv &&) = delete;
        ~AwsEnv()
        {
            Aws::ShutdownAPI(options_);
        }

    private:
        Aws::SDKOptions options_;
    };

    /// @brief Factory class for accessing AWS environment. It guarantees that at a single point in time
    /// only one instance of AwsEnv is active, so that AWS environment is initialized just once.
    class AwsEnvFactory
    {
    private:
        AwsEnvFactory() = default;
        ~AwsEnvFactory() = default;

    public:
        /// @brief Create a new instance of AwsEnv, or return the cached instance if it exists.
        /// @return A shared pointer to an AwsEnv instance.
        static std::shared_ptr<AwsEnv> create();

    private:
        static std::weak_ptr<AwsEnv> cached_instance_;

        static std::mutex instance_access_;
    };

} // namespace gst::airtime