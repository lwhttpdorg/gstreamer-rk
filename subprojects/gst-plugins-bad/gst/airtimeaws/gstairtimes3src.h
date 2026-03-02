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

/**
 * SECTION:element-airtimes3src
 *
 * @title: airtimes3src
 * @short_description: A src element that provides URI-based, seekable, cached access to AWS S3 locations.
 * Caching is performed in an async manner by a pool of threads, allowing for efficient access to large files. Caching
 * is designed to survive between pipeline runs, or to be used by multiple pipelines running inside the same process.
 *
 * The airtimes3src element has caching abilities that can be configured via properties. Caching happens in the form of
 * local file chunks, which are downloaded from S3 and stored in a local cache directory. Common eviction policies are
 * in place to manage the cache size and remove old or unused chunks. Partially cached files can be used to resume S3
 * downloads. Incomplete downloads can be resumed, and the element will attempt to restore the download state from the
 * cache.
 *
 * Buffer filling allows for efficient access to large files. This means that the pipeline is allowed to start
 * processing data even if the entire file is not yet fully downloaded.
 *
 * Download priority is given to the byte ranges requested, which means that seeking is fully supported. The element
 * will prioritize downloading the requested byte ranges first, and then fill in the rest of the file as needed.
 *
 * Note that because this is a download-based source, while real-time playback can be achieved during downloading, it is
 * not guaranteed and fully depends on the network bandwidth and AWS (S3) response times.
 *
 * The element registers itself as a URI handler for S3 URIs, which means that it can be used in GStreamer pipelines,
 * for discovery purposes, in GES projects, etc.
 *
 * The element posts metrics to the bus, which includes the location, content length, download completion state, etc. To
 * listen to these metrics, look for the `airtimes3src::metrics` message.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -v airtimes3src location='s3://bucket/clip.mp4' ! decodebin ! videoconvert ! autovideosink
 *
 * ]|
 * </refsect2>
 */

#pragma once

#include <gst/base/gstbasesrc.h>
#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_AIRTIMES3SRC (gst_airtime_s3_src_get_type())
G_DECLARE_FINAL_TYPE(GstAirtimeS3Src, gst_airtime_s3_src, GST, AIRTIMES3SRC, GstBaseSrc)

/* Element registration function declaration */
GST_ELEMENT_REGISTER_DECLARE(airtime_s3_src)

/**
 * GstAirtimeS3SrcSourceHintType:
 * @S3_SOURCE_HINT_TYPE_NONE: no specific hint
 * @S3_SOURCE_HINT_TYPE_KEY: hint that the source is a specific S3 key
 * @S3_SOURCE_HINT_TYPE_PREFIX: hint that the source is an S3 prefix (directory)
 */
typedef enum
{
    S3_SOURCE_HINT_TYPE_NONE,
    S3_SOURCE_HINT_TYPE_KEY,
    S3_SOURCE_HINT_TYPE_PREFIX,
} GstAirtimeS3SrcSourceHintType;

G_END_DECLS
