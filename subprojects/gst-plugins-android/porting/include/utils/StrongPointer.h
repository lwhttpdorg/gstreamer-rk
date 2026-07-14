/* Alias header — AOSP code often `#include <utils/StrongPointer.h>` to get sp<>. */
 * SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef GST_C2_PORTING_UTILS_STRONGPOINTER_H_
#define GST_C2_PORTING_UTILS_STRONGPOINTER_H_

/* AData.h (pulled in by AMessage.h) uses memset() and std::numeric_limits but
 * only #includes <memory>/<type_traits>, relying on bionic's headers to drag
 * in <cstring>/<limits> transitively. libstdc++/glibc make no such promise, so
 * AMessage.cpp fails with "memset/numeric_limits not declared". AData.h
 * #includes <utils/StrongPointer.h> *before* it uses those symbols, so sourcing
 * the missing standard headers here fixes it without editing the vendored AOSP
 * AData.h. */
#include <cstring>
#include <limits>

#include <utils/RefBase.h>
#endif
