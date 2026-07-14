/*
 * gst-plugins-android — stub for the HIDL bufferpool V2_0 IAccessor.
 *
 * C2BufferPriv.h:23 includes it and names android::...::V2_0::IAccessor only as
 * an sp<> in getAccessor() (C2BufferPriv.h:122). An empty RefBase-derived type
 * is enough — the audio path never obtains an accessor.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef GST_C2_PORTING_BUFFERPOOL_V2_0_IACCESSOR_STUB_H_
#define GST_C2_PORTING_BUFFERPOOL_V2_0_IACCESSOR_STUB_H_

#include <utils/RefBase.h>

namespace android {
namespace hardware {
namespace media {
namespace bufferpool {
namespace V2_0 {

struct IAccessor : public ::android::RefBase {};

}  // namespace V2_0
}  // namespace bufferpool
}  // namespace media
}  // namespace hardware
}  // namespace android

#endif
