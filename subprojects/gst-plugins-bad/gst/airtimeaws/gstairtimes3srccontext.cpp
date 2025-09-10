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

#include "gstairtimes3srccontext.h"
#include "gstairtimes3srccontext.hpp"
#include "gstairtimes3uriproviderconfig.hpp"
#include "gstairtimes3uriproviders.hpp"
#include "gstairtimes3uriprovidersfactory.hpp"

#include <gst/gst.h>

#include <memory>
#include <mutex>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

using namespace gst::airtime;

GST_DEBUG_CATEGORY_STATIC(gst_airtime_s3_src_context_debug);
#define GST_CAT_DEFAULT gst_airtime_s3_src_context_debug

namespace
{

struct GstAirtimeS3SrcContextPrivateGuts {
    explicit GstAirtimeS3SrcContextPrivateGuts(S3URIProviderConfig config) :
        config_{std::move(config)},
        s3_uri_providers_{S3URIProvidersFactory::create(config_)}
    {
    }
    ~GstAirtimeS3SrcContextPrivateGuts()
    {
        GST_DEBUG("Destroying S3 src context guts");
    }

private:
    S3URIProviderConfig config_;
    std::pair<std::shared_ptr<S3URIProviders>, bool> s3_uri_providers_;
};

} // namespace

struct _GstAirtimeS3SrcContextPrivate {
    // @note _GstAirtimeS3SrcContextPrivate object is allocated with malloc by C runtime. Any C++ object created on
    // stack in this struct would need to be manually allocated with placement new, and deleted with placement delete.
    // Alternatively, we can use yet another pointer indirection and handle it with new/delete to make guts' objects
    // properly created/destroyed (with constructors and destructors). We do the latter here.
    GstAirtimeS3SrcContextPrivateGuts* guts;
};

#define gst_airtime_s3_src_context_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE(GstAirtimeS3SrcContext, gst_airtime_s3_src_context, GST_TYPE_OBJECT);

static void gst_airtime_s3_src_context_finalize(GObject* object)
{
    GstAirtimeS3SrcContext* self = GST_AIRTIME_S3_SRC_CONTEXT_CAST(object);

    GST_DEBUG_OBJECT(self, "Finalizing airtime S3 src context");

    if (self->impl->guts)
    {
        GST_DEBUG_OBJECT(self, "Destroying S3 src context guts");
        delete self->impl->guts;
        self->impl->guts = nullptr;
    }
    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void gst_airtime_s3_src_context_init(GstAirtimeS3SrcContext* self)
{
    GST_DEBUG_OBJECT(self, "Initializing airtime S3 src context");
}

static void gst_airtime_s3_src_context_class_init(GstAirtimeS3SrcContextClass* klass)
{
    GObjectClass* gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = gst_airtime_s3_src_context_finalize;

    GST_DEBUG_CATEGORY_INIT(gst_airtime_s3_src_context_debug, "airtimes3srccontext", 0, "airtime S3 src context");
}

namespace gst::airtime
{

GstAirtimeS3SrcContext* gst_airtime_s3_src_context_new(S3URIProviderConfig config) noexcept
{
    GstAirtimeS3SrcContext* self;

    self = static_cast<GstAirtimeS3SrcContext*>(g_object_new(GST_TYPE_AIRTIME_S3_SRC_CONTEXT, nullptr));
    self->impl = static_cast<GstAirtimeS3SrcContextPrivate*>(gst_airtime_s3_src_context_get_instance_private(self));
    if (not self->impl->guts)
    {
        GST_DEBUG_OBJECT(self, "Creating airtime S3 src context");
        try
        {
            self->impl->guts = new GstAirtimeS3SrcContextPrivateGuts(std::move(config));
        }
        catch (const std::exception& e)
        {
            GST_ERROR_OBJECT(self, "Failed to create airtime S3 src context: %s", e.what());
            g_object_unref(self);
            return nullptr;
        }
    }
    return self;
}

} // namespace gst::airtime