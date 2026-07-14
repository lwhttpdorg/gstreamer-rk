// SPDX-License-Identifier: LGPL-2.1-or-later
// Stub: AStringUtils — only used by ADebug.cpp log-tag matching (not critical).
#include <media/stagefright/foundation/AStringUtils.h>
#include <cstring>

namespace android {

int AStringUtils::Compare(const char *a, const char *b, size_t len, bool ignoreCase) {
    return ignoreCase ? strncasecmp(a, b, len) : strncmp(a, b, len);
}

bool AStringUtils::MatchesGlob(
        const char * /*glob*/, size_t /*globLen*/,
        const char * /*str*/, size_t /*strLen*/, bool /*ignoreCase*/) {
    return true;  // always match — we don't filter logs by tag glob
}

}  // namespace android
