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

#include "gstairtimes3error.hpp"

namespace gst::airtime
{

std::string S3ErrorCategory::message(int ev) const
{
    switch (static_cast<Aws::S3::S3Errors>(ev))
    {
        // Core errors
        case Aws::S3::S3Errors::INCOMPLETE_SIGNATURE:
            return "The request signature is incomplete";
        case Aws::S3::S3Errors::INTERNAL_FAILURE:
            return "AWS S3 internal server error";
        case Aws::S3::S3Errors::INVALID_ACTION:
            return "The requested action is not valid";
        case Aws::S3::S3Errors::INVALID_CLIENT_TOKEN_ID:
            return "The provided AWS access key ID is invalid";
        case Aws::S3::S3Errors::INVALID_PARAMETER_COMBINATION:
            return "Parameters that must not be used together were used together";
        case Aws::S3::S3Errors::INVALID_QUERY_PARAMETER:
            return "An invalid query parameter was specified";
        case Aws::S3::S3Errors::INVALID_PARAMETER_VALUE:
            return "An invalid parameter value was specified";
        case Aws::S3::S3Errors::MISSING_ACTION:
            return "The request is missing a required action";
        case Aws::S3::S3Errors::MISSING_AUTHENTICATION_TOKEN:
            return "The request is missing an authentication token";
        case Aws::S3::S3Errors::MISSING_PARAMETER:
            return "The request is missing a required parameter";
        case Aws::S3::S3Errors::OPT_IN_REQUIRED:
            return "AWS S3 access requires opt-in";
        case Aws::S3::S3Errors::REQUEST_EXPIRED:
            return "The request has expired";
        case Aws::S3::S3Errors::SERVICE_UNAVAILABLE:
            return "AWS S3 service is unavailable";
        case Aws::S3::S3Errors::THROTTLING:
            return "Request was throttled due to request rate";
        case Aws::S3::S3Errors::VALIDATION:
            return "Request validation error";
        case Aws::S3::S3Errors::ACCESS_DENIED:
            return "Access denied to AWS S3 resource";
        case Aws::S3::S3Errors::RESOURCE_NOT_FOUND:
            return "The requested AWS S3 resource was not found";
        case Aws::S3::S3Errors::UNRECOGNIZED_CLIENT:
            return "The client is not recognized by AWS S3";
        case Aws::S3::S3Errors::MALFORMED_QUERY_STRING:
            return "The query string is malformed";
        case Aws::S3::S3Errors::SLOW_DOWN:
            return "Request rate is too high, please slow down";
        case Aws::S3::S3Errors::REQUEST_TIME_TOO_SKEWED:
            return "Request time is too skewed from AWS server time";
        case Aws::S3::S3Errors::INVALID_SIGNATURE:
            return "The request signature is invalid";
        case Aws::S3::S3Errors::SIGNATURE_DOES_NOT_MATCH:
            return "The provided signature does not match the calculated signature";
        case Aws::S3::S3Errors::INVALID_ACCESS_KEY_ID:
            return "The provided AWS access key ID is invalid";
        case Aws::S3::S3Errors::REQUEST_TIMEOUT:
            return "The request timed out";
        case Aws::S3::S3Errors::NETWORK_CONNECTION:
            return "Network connection error while communicating with AWS S3";
        case Aws::S3::S3Errors::UNKNOWN:
            return "Unknown AWS S3 error";

        // S3 specific errors
        case Aws::S3::S3Errors::BUCKET_ALREADY_EXISTS:
            return "Bucket already exists";
        case Aws::S3::S3Errors::BUCKET_ALREADY_OWNED_BY_YOU:
            return "Bucket already owned by you";
        case Aws::S3::S3Errors::ENCRYPTION_TYPE_MISMATCH:
            return "Encryption type mismatch";
        case Aws::S3::S3Errors::INVALID_OBJECT_STATE:
            return "Invalid object state";
        case Aws::S3::S3Errors::INVALID_REQUEST:
            return "Invalid request";
        case Aws::S3::S3Errors::INVALID_WRITE_OFFSET:
            return "Invalid write offset";
        case Aws::S3::S3Errors::NO_SUCH_BUCKET:
            return "No such bucket";
        case Aws::S3::S3Errors::NO_SUCH_KEY:
            return "No such key";
        case Aws::S3::S3Errors::NO_SUCH_UPLOAD:
            return "No such upload";
        case Aws::S3::S3Errors::OBJECT_ALREADY_IN_ACTIVE_TIER:
            return "Object already in active tier";
        case Aws::S3::S3Errors::OBJECT_NOT_IN_ACTIVE_TIER:
            return "Object not in active tier";
        case Aws::S3::S3Errors::TOO_MANY_PARTS:
            return "Too many parts";

        default:
            return "Unknown S3 error code: " + std::to_string(ev);
    }
}

namespace
{

const S3ErrorCategory s3_error_category;

} // namespace

std::error_code make_error_code(Aws::S3::S3Errors e)
{
    return std::error_code(static_cast<int>(e), s3_error_category);
}

} // namespace gst::airtime
