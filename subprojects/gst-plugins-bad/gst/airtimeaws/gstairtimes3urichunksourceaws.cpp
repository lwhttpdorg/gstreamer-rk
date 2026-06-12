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

#include "gstairtimes3urichunksourceaws.hpp"
#include "gstairtimes3error.hpp"
#include "gstairtimes3utils.hpp"

#include <aws/s3/model/GetBucketLocationRequest.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/HeadObjectRequest.h>
#include <aws/sts/STSClient.h>
#include <aws/sts/model/GetCallerIdentityRequest.h>

#include <gst/gst.h>

namespace gst::airtime
{

    Aws::S3::S3ClientConfiguration
    createS3ClientConfiguration(std::shared_ptr<Aws::Utils::Threading::PooledThreadExecutor> thread_pool_executor,
                                long http_request_timeout_ms, long request_timeout_ms)
    {
        Aws::S3::S3ClientConfiguration config{};
        config.executor = std::move(thread_pool_executor);
        config.useVirtualAddressing = false;

        // https://docs.aws.amazon.com/sdk-for-cpp/latest/api/aws-cpp-sdk-core/html/struct_aws_1_1_client_1_1_client_configuration.html#a68c35ac8d14619e4bfc77d848fd89473
        config.requestTimeoutMs = request_timeout_ms;

        // https://docs.aws.amazon.com/sdk-for-cpp/latest/api/aws-cpp-sdk-core/html/struct_aws_1_1_client_1_1_client_configuration.html#a7f812c185f0363d21fe706ca1117c56b
        config.httpRequestTimeoutMs = http_request_timeout_ms;

        return config;
    }

    std::shared_ptr<const Aws::Client::AsyncCallerContext> AwsActiveAsyncS3Requests::createRequestContext()
    {
        std::lock_guard lock{requests_access_};
        return active_requests_.emplace_back(
            Aws::MakeShared<Aws::Client::AsyncCallerContext>("airtime_s3_download_context"));
    }

    void AwsActiveAsyncS3Requests::deleteRequestContext(std::shared_ptr<const Aws::Client::AsyncCallerContext> context)
    {
        std::lock_guard lock{requests_access_};
        auto it = std::find(active_requests_.begin(), active_requests_.end(), context);
        if (it != active_requests_.end())
        {
            active_requests_.erase(it);
        }
    }

    void AwsActiveAsyncS3Requests::clear()
    {
        std::lock_guard lock{requests_access_};
        active_requests_.clear();
    }

    bool AwsActiveAsyncS3Requests::empty() const
    {
        std::lock_guard lock{requests_access_};
        return active_requests_.empty();
    }

    std::size_t AwsActiveAsyncS3Requests::size() const
    {
        std::lock_guard lock{requests_access_};
        return active_requests_.size();
    }

    // --------------------------------------------------------------------------------------------------------------------

    S3URIChunkSourceAws::S3URIChunkSourceAws(std::string s3_bucket, std::string s3_key, std::size_t max_number_of_downloads,
                                             long http_request_timeout_ms, long request_timeout_ms,
                                             bool validate_credentials, bool ensure_correct_region) : s3_bucket_{std::move(s3_bucket)},
                                                                                                      s3_key_{std::move(s3_key)},
                                                                                                      aws_env_{AwsEnvFactory::create()},
                                                                                                      thread_pool_executor_{Aws::MakeShared<Aws::Utils::Threading::PooledThreadExecutor>(
                                                                                                          "AIRTIMES3SRC_THREAD_POOL_EXECUTOR", max_number_of_downloads)},
                                                                                                      http_request_timeout_ms_{http_request_timeout_ms},
                                                                                                      request_timeout_ms_{request_timeout_ms},
                                                                                                      s3_client_{Aws::MakeShared<Aws::S3::S3Client>(
                                                                                                          "AIRTIMES3SRC_S3_CLIENT",
                                                                                                          createS3ClientConfiguration(thread_pool_executor_, http_request_timeout_ms_, request_timeout_ms_))}
    {
        if (validate_credentials)
        {
            checkCredentials();
        }
        if (ensure_correct_region)
        {
            ensureCorrectBucketRegion(s3_client_, s3_bucket_, thread_pool_executor_, http_request_timeout_ms_,
                                      request_timeout_ms_);
        }
    }

    S3URIChunkSourceAws::~S3URIChunkSourceAws()
    {
        try
        {
            cancel();
        }
        catch (const std::exception &e)
        {
            GST_ERROR("Exception during S3URIChunkSourceAws destruction: %s", e.what());
        }
        catch (...)
        {
            GST_ERROR("Unknown exception during S3URIChunkSourceAws destruction");
        }
    }

    std::pair<std::error_code, S3URIObjectMetadata> S3URIChunkSourceAws::getObjectMetadata()
    {
        GST_DEBUG("Retrieving S3 object metadata for bucket: %s, key: %s", s3_bucket_.c_str(), s3_key_.c_str());
        // perform a head object operation to get the content length
        Aws::S3::Model::HeadObjectRequest head_object_request;
        head_object_request.SetBucket(s3_bucket_);
        head_object_request.SetKey(s3_key_);

        Aws::S3::Model::HeadObjectOutcome head_object_outcome = s3_client_->HeadObject(head_object_request);

        if (not head_object_outcome.IsSuccess())
        {
            GST_ERROR("Error retrieving S3 object metadata: %s", head_object_outcome.GetError().GetMessage().c_str());
            return {make_error_code(head_object_outcome.GetError().GetErrorType()), {}};
        }

        // successfully retrieved object metadata
        auto &head_object_result = head_object_outcome.GetResult();
        const long long content_length = head_object_result.GetContentLength();
        if (content_length < 0)
        {
            GST_ERROR("Invalid content length: %lld", content_length);
            throw std::runtime_error("Invalid content length received from S3");
        }
        S3URIObjectMetadata metadata{.bucket = s3_bucket_,
                                     .key = s3_key_,
                                     .content_length = static_cast<std::uint64_t>(content_length),
                                     .entity_tag = unescapeString(head_object_result.GetETag()),
                                     .version_id = head_object_result.GetVersionId(),
                                     .expiration = head_object_result.GetExpiration(),
                                     .last_modified =
                                         head_object_result.GetLastModified().ToGmtString(Aws::Utils::DateFormat::RFC822)};
        GST_INFO("Object metadata retrieved successfully.");
        GST_DEBUG("S3 Object Metadata:");
        GST_DEBUG("Content Type: %s", head_object_result.GetContentType().c_str());
        GST_DEBUG("Content Length: %" G_GUINT64_FORMAT, metadata.content_length);
        GST_DEBUG("Entity Tag: %s", metadata.entity_tag.c_str());
        GST_DEBUG("Version ID: %s", metadata.version_id.empty() ? "N/A" : metadata.version_id.c_str());
        GST_DEBUG("Expiration: %s", metadata.expiration.empty() ? "N/A" : metadata.expiration.c_str());
        GST_DEBUG("Last Modified: %s", metadata.last_modified.c_str());

        return {{}, std::move(metadata)};
    }

    namespace
    {

        void getObjectAsyncCallbackImpl(const Aws::S3::Model::GetObjectOutcome &outcome, S3URIChunkSpec chunk_spec,
                                        const S3URIChunkSource::DownloadedChunkCallback &callback)
        {
            try
            {
                if (outcome.IsSuccess())
                {
                    auto &s3_get_object_result = outcome.GetResult();
                    Aws::IOStream &s3_get_object_body = s3_get_object_result.GetBody();
                    callback(std::error_code{}, std::move(chunk_spec), &s3_get_object_body);
                }
                else
                {
                    callback(make_error_code(outcome.GetError().GetErrorType()), std::move(chunk_spec), nullptr);
                }
            }
            catch (const std::exception &e)
            {
                GST_ERROR("Exception in getObjectAsyncCallbackImpl: %s", e.what());
            }
            catch (...)
            {
                GST_ERROR("Unknown exception in getObjectAsyncCallbackImpl");
            }
        }

    } // namespace

    void S3URIChunkSourceAws::downloadChunkAsync(S3URIChunkSpec chunk_spec, DownloadedChunkCallback callback)
    {
        GST_DEBUG("%s", std::format("Preparing to download chunk, bucket: {}, key:{}, bytes={}-{}", s3_bucket_, s3_key_,
                                    chunk_spec.startByte(), chunk_spec.endByte())
                            .c_str());
        Aws::S3::Model::GetObjectRequest request{};
        request.SetBucket(s3_bucket_);
        request.SetKey(s3_key_);

        // format the range header to the specified byte range
        auto range = std::format("bytes={}-{}", chunk_spec.startByte(), chunk_spec.endByte());
        request.SetRange(std::move(range));

        GST_DEBUG("Initiating async download for chunk: %" G_GSIZE_FORMAT " with range: %s", chunk_spec.index(),
                  request.GetRange().c_str());

        auto context = active_async_requests_.createRequestContext();
        s3_client_->GetObjectAsync(
            request,
            [this, chunk_spec = std::move(chunk_spec),
             callback = std::move(callback)](const Aws::S3::S3Client *, const Aws::S3::Model::GetObjectRequest &,
                                             const Aws::S3::Model::GetObjectOutcome &outcome,
                                             const std::shared_ptr<const Aws::Client::AsyncCallerContext> &context)
            {
                getObjectAsyncCallbackImpl(outcome, std::move(chunk_spec), callback);
                active_async_requests_.deleteRequestContext(context);
            },
            context);
    }

    std::size_t S3URIChunkSourceAws::activeRequests() const
    {
        return active_async_requests_.size();
    }

    void S3URIChunkSourceAws::cancel()
    {
        thread_pool_executor_->WaitUntilStopped();
        s3_client_.reset();
        active_async_requests_.clear();
        GST_DEBUG("All S3 operations cancelled");
    }

    void checkCredentials()
    {
        GST_DEBUG("Checking AWS credentials for S3 access");
        Aws::STS::STSClient sts_client;
        Aws::STS::Model::GetCallerIdentityRequest request;
        auto outcome = sts_client.GetCallerIdentity(request);

        if (outcome.IsSuccess())
        {
            return;
        }

        auto error = outcome.GetError();
        if (error.GetErrorType() == Aws::STS::STSErrors::EXPIRED_TOKEN)
        {
            throw std::runtime_error{"AWS credentials have expired. Please refresh your credentials."};
        }
        else if (error.GetErrorType() == Aws::STS::STSErrors::INVALID_CLIENT_TOKEN_ID)
        {
            throw std::runtime_error{"Invalid AWS credentials. Please check your access key and secret key."};
        }
        else
        {
            throw std::runtime_error{error.GetMessage()};
        }
    }

    void ensureCorrectBucketRegion(std::shared_ptr<Aws::S3::S3Client> &s3_client, const std::string &bucket_name,
                                   std::shared_ptr<Aws::Utils::Threading::PooledThreadExecutor> thread_pool_executor,
                                   long http_request_timeout_ms, long request_timeout_ms)
    {
        GST_DEBUG("Ensuring correct S3 bucket region for bucket: %s", bucket_name.c_str());
        Aws::S3::Model::GetBucketLocationRequest location_request;
        location_request.SetBucket(bucket_name);

        auto location_outcome = s3_client->GetBucketLocation(location_request);
        if (location_outcome.IsSuccess())
        {
            auto &result = location_outcome.GetResult();
            auto region_str = Aws::S3::Model::BucketLocationConstraintMapper::GetNameForBucketLocationConstraint(
                result.GetLocationConstraint());
            Aws::S3::S3ClientConfiguration s3_client_config =
                createS3ClientConfiguration(std::move(thread_pool_executor), http_request_timeout_ms, request_timeout_ms);
            if (not region_str.empty() and region_str != s3_client_config.region)
            {
                s3_client_config.region = std::move(region_str);
                s3_client = Aws::MakeShared<Aws::S3::S3Client>("AIRTIMES3SRC_S3_CLIENT", s3_client_config);
                GST_DEBUG("Reconfigured S3 client for region: %s", s3_client_config.region.c_str());
            }
        }
        else
        {
            auto error_message = std::format("Error getting bucket location: {}", location_outcome.GetError().GetMessage());
            GST_ERROR("%s", error_message.c_str());
            throw std::runtime_error{std::move(error_message)};
        }
    }

} // namespace gst::airtime