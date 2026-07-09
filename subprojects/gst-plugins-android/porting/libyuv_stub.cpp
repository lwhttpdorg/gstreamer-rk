/*
 * gst-plugins-android — libyuv link-symbol stubs.
 *
 * Definitions for the libyuv conversion helpers declared in <libyuv.h>. These
 * are reachable only from SimpleC2Component's VIDEO output path, which the
 * audio decoders never execute. Each returns -1 (error) if ever called.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include <libyuv.h>

namespace libyuv {

struct YuvConstants { int _unused; };
const YuvConstants kYuvV2020Constants{0};

int I420ToI420(...)        { return -1; }
int I422ToI420(...)        { return -1; }
int I444ToI420(...)        { return -1; }
int I010ToI420(...)        { return -1; }
int I010ToP010(...)        { return -1; }
int I210ToI420(...)        { return -1; }
int I210ToI010(...)        { return -1; }
int I210ToP010(...)        { return -1; }
int I210ToAB30Matrix(...)  { return -1; }
int I410ToI420(...)        { return -1; }
int I410ToI010(...)        { return -1; }
int I410ToP010(...)        { return -1; }
int I410ToAB30Matrix(...)  { return -1; }

}  /* namespace libyuv */
