/* gst-plugins-android — <android-base/thread_annotations.h> no-op shim. */
 * SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef GST_C2_PORTING_ANDROID_BASE_THREAD_ANNOTATIONS_H_
#define GST_C2_PORTING_ANDROID_BASE_THREAD_ANNOTATIONS_H_

#define GUARDED_BY(x)
#define PT_GUARDED_BY(x)
#define ACQUIRED_AFTER(...)
#define ACQUIRED_BEFORE(...)
#define EXCLUSIVE_LOCKS_REQUIRED(...)
#define SHARED_LOCKS_REQUIRED(...)
#define LOCKS_EXCLUDED(...)
#define LOCK_RETURNED(x)
#define LOCKABLE
#define SCOPED_LOCKABLE
#define EXCLUSIVE_LOCK_FUNCTION(...)
#define SHARED_LOCK_FUNCTION(...)
#define EXCLUSIVE_TRYLOCK_FUNCTION(...)
#define SHARED_TRYLOCK_FUNCTION(...)
#define UNLOCK_FUNCTION(...)
#define NO_THREAD_SAFETY_ANALYSIS
#define ASSERT_EXCLUSIVE_LOCK(...)
#define ASSERT_SHARED_LOCK(...)
#define REQUIRES(...)
#define REQUIRES_SHARED(...)
#define EXCLUDES(...)
#define RETURN_CAPABILITY(x)
#define CAPABILITY(x)
#define SCOPED_CAPABILITY
#define TRY_ACQUIRE(...)
#define ACQUIRE(...)
#define RELEASE(...)
#define RELEASE_SHARED(...)
#define ACQUIRE_SHARED(...)
#define TRY_ACQUIRE_SHARED(...)
#define NO_THREAD_SAFETY_ANALYSIS_

#endif
