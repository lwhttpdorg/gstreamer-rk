#pragma once

#include "gstairtimes3srccontext.h"
#include "gstairtimes3uriproviderconfig.hpp"

namespace gst::airtime
{

/// @brief Helper function for constructing a #GstAirtimeS3SrcContext. Constructs the #GstAirtimeS3SrcContext with the
/// given config parameter, and creates the S3 cache instance.
/// @param config S3 URI provider configuration.
/// @return a new #GstAirtimeS3SrcContext instance or nullptr on error.
GstAirtimeS3SrcContext* gst_airtime_s3_src_context_new(S3URIProviderConfig config) noexcept;

} // namespace gst::airtime