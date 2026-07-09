#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Fetch the AOSP Codec2 source files we vendor for gst-plugins-android.

The list is encoded in this script so a fresh checkout needs no extra metadata.
Each entry maps a `dest` relative path (under --out) to the gitiles `?format=TEXT`
URL on android.googlesource.com — that endpoint returns the file's raw bytes
base64-encoded, which is the only way to fetch raw source from gitiles without
git.

Usage:
    scripts/fetch_aosp_sources.py --out aosp
    scripts/fetch_aosp_sources.py --out aosp --tag android-16.0.0_r1
    scripts/fetch_aosp_sources.py --out aosp --tarball-mirror /path/to/aosp.tar.xz

The fetch is idempotent: files already present with non-zero size are skipped
unless --force is passed. A `.fetch.stamp` file is written on success and is
the marker meson uses to decide whether to re-fetch.
"""
from __future__ import annotations

import argparse
import base64
import concurrent.futures as futures
import hashlib
import os
import random
import sys
import tarfile
import time
import urllib.error
import urllib.request
from pathlib import Path

DEFAULT_TAG = "android-16.0.0_r1"

# Source repositories we vendor from. Most files come from frameworks/av; the
# Tremolo (fixed-point Vorbis) decoder lives in its own repo. Each entry's
# raw gitiles URL is REPO_TEMPLATES[repo].format(tag=tag, path=path) + '?format=TEXT'.
REPO_TEMPLATES = {
    "av":      "https://android.googlesource.com/platform/frameworks/av/+/refs/tags/{tag}/{path}?format=TEXT",
    "tremolo": "https://android.googlesource.com/platform/external/tremolo/+/refs/tags/{tag}/{path}?format=TEXT",
}
# Backwards-compat alias used by older code paths.
URL_TEMPLATE = REPO_TEMPLATES["av"]

# Entries are either (dest, aosp_path) — implying repo 'av' — or
# (dest, repo, aosp_path). _normalize() turns both into (dest, repo, path).
def _normalize(entry):
    if len(entry) == 2:
        return (entry[0], "av", entry[1])
    return entry

# (dest_relative_to_out, [repo,] aosp_relative_path)
FILES = [
    # ── Codec2 core: public API headers + the lone C2.cpp ────────────────
    ("core/include/C2.h",               "media/codec2/core/include/C2.h"),
    ("core/include/_C2MacroUtils.h",    "media/codec2/core/include/_C2MacroUtils.h"),
    ("core/include/C2Enum.h",           "media/codec2/core/include/C2Enum.h"),
    ("core/include/C2Param.h",          "media/codec2/core/include/C2Param.h"),
    ("core/include/C2ParamDef.h",       "media/codec2/core/include/C2ParamDef.h"),
    ("core/include/C2BufferBase.h",     "media/codec2/core/include/C2BufferBase.h"),
    ("core/include/C2Buffer.h",         "media/codec2/core/include/C2Buffer.h"),
    ("core/include/C2Work.h",           "media/codec2/core/include/C2Work.h"),
    ("core/include/C2Component.h",      "media/codec2/core/include/C2Component.h"),
    ("core/include/C2Config.h",         "media/codec2/core/include/C2Config.h"),
    ("core/C2.cpp",                     "media/codec2/core/C2.cpp"),
    # C2ComponentFactory.h lives under vndk/include, not core, in android-16
    # but the AOSP SimpleC2Component code expects it on the include path
    # without the 'vndk/' prefix — copy to a flat dest so an `#include <C2ComponentFactory.h>`
    # resolves through aosp/vndk/include which is already on -I.
    ("vndk/include/C2ComponentFactory.h", "media/codec2/vndk/include/C2ComponentFactory.h"),
    ("vndk/include/C2AllocatorBlob.h",    "media/codec2/vndk/include/C2AllocatorBlob.h"),

    # ── Codec2 vndk: linear/buffer/interface helpers we actually compile ─
    ("vndk/include/C2BufferPriv.h",                 "media/codec2/vndk/include/C2BufferPriv.h"),
    ("vndk/include/C2Debug.h",                      "media/codec2/vndk/include/C2Debug.h"),
    ("vndk/include/C2ErrnoUtils.h",                 "media/codec2/vndk/include/C2ErrnoUtils.h"),
    ("vndk/include/C2PlatformSupport.h",            "media/codec2/vndk/include/C2PlatformSupport.h"),
    ("vndk/include/util/C2Debug-base.h",            "media/codec2/vndk/include/util/C2Debug-base.h"),
    ("vndk/include/util/C2Debug-interface.h",       "media/codec2/vndk/include/util/C2Debug-interface.h"),
    ("vndk/include/util/C2Debug-log.h",             "media/codec2/vndk/include/util/C2Debug-log.h"),
    ("vndk/include/util/C2Debug-param.h",           "media/codec2/vndk/include/util/C2Debug-param.h"),
    ("vndk/include/util/C2InterfaceHelper.h",       "media/codec2/vndk/include/util/C2InterfaceHelper.h"),
    ("vndk/include/util/C2InterfaceUtils.h",        "media/codec2/vndk/include/util/C2InterfaceUtils.h"),
    ("vndk/include/util/C2ParamUtils.h",            "media/codec2/vndk/include/util/C2ParamUtils.h"),
    ("vndk/internal/C2BlockInternal.h",             "media/codec2/vndk/internal/C2BlockInternal.h"),
    ("vndk/internal/C2HandleIonInternal.h",         "media/codec2/vndk/internal/C2HandleIonInternal.h"),
    ("vndk/internal/C2ParamInternal.h",             "media/codec2/vndk/internal/C2ParamInternal.h"),
    ("vndk/C2Buffer.cpp",                           "media/codec2/vndk/C2Buffer.cpp"),
    ("vndk/C2Config.cpp",                           "media/codec2/vndk/C2Config.cpp"),
    ("vndk/util/C2Debug.cpp",                       "media/codec2/vndk/util/C2Debug.cpp"),
    ("vndk/util/C2InterfaceHelper.cpp",             "media/codec2/vndk/util/C2InterfaceHelper.cpp"),
    ("vndk/util/C2InterfaceUtils.cpp",              "media/codec2/vndk/util/C2InterfaceUtils.cpp"),
    ("vndk/util/C2ParamUtils.cpp",                  "media/codec2/vndk/util/C2ParamUtils.cpp"),

    # ── SimpleC2Component base and per-codec SW components ───────────────
    ("components/base/include/SimpleC2Component.h", "media/codec2/components/base/include/SimpleC2Component.h"),
    ("components/base/include/SimpleC2Interface.h", "media/codec2/components/base/include/SimpleC2Interface.h"),
    ("components/base/SimpleC2Component.cpp",       "media/codec2/components/base/SimpleC2Component.cpp"),
    ("components/base/SimpleC2Interface.cpp",       "media/codec2/components/base/SimpleC2Interface.cpp"),
    ("components/opus/C2SoftOpusDec.h",             "media/codec2/components/opus/C2SoftOpusDec.h"),
    ("components/opus/C2SoftOpusDec.cpp",           "media/codec2/components/opus/C2SoftOpusDec.cpp"),
    ("components/aac/C2SoftAacDec.h",               "media/codec2/components/aac/C2SoftAacDec.h"),
    ("components/aac/C2SoftAacDec.cpp",             "media/codec2/components/aac/C2SoftAacDec.cpp"),
    ("components/aac/DrcPresModeWrap.h",            "media/codec2/components/aac/DrcPresModeWrap.h"),
    ("components/aac/DrcPresModeWrap.cpp",          "media/codec2/components/aac/DrcPresModeWrap.cpp"),

    # ── libstagefright/foundation: AMessage / ALooper / AHandler / AString ─
    # NOTE: In Android 16 (android-16.0.0_r1) the canonical location migrated
    # from media/libstagefright/foundation/ to media/module/foundation/. The
    # public include path the consumers see is still
    # `media/stagefright/foundation/...` because that layout is preserved under
    # module/foundation/include/. We keep the *destination* directory in our
    # vendored tree named "libstagefright/foundation" to match the design doc
    # and meson layout; only the upstream URL changes.
    ("libstagefright/foundation/include/media/stagefright/foundation/AAtomizer.h",
        "media/module/foundation/include/media/stagefright/foundation/AAtomizer.h"),
    ("libstagefright/foundation/include/media/stagefright/foundation/ABase.h",
        "media/module/foundation/include/media/stagefright/foundation/ABase.h"),
    ("libstagefright/foundation/include/media/stagefright/foundation/ABitReader.h",
        "media/module/foundation/include/media/stagefright/foundation/ABitReader.h"),
    ("libstagefright/foundation/include/media/stagefright/foundation/AData.h",
        "media/module/foundation/include/media/stagefright/foundation/AData.h"),
    ("libstagefright/foundation/include/media/stagefright/foundation/ABuffer.h",
        "media/module/foundation/include/media/stagefright/foundation/ABuffer.h"),
    ("libstagefright/foundation/include/media/stagefright/foundation/AHandlerReflector.h",
        "media/module/foundation/include/media/stagefright/foundation/AHandlerReflector.h"),
    ("libstagefright/foundation/include/media/stagefright/foundation/AStringUtils.h",
        "media/module/foundation/include/media/stagefright/foundation/AStringUtils.h"),
    ("libstagefright/foundation/include/media/stagefright/foundation/TypeTraits.h",
        "media/module/foundation/include/media/stagefright/foundation/TypeTraits.h"),
    ("libstagefright/foundation/include/media/stagefright/foundation/ColorUtils.h",
        "media/module/foundation/include/media/stagefright/foundation/ColorUtils.h"),
    ("libstagefright/foundation/include/media/stagefright/foundation/Flagged.h",
        "media/module/foundation/include/media/stagefright/foundation/Flagged.h"),
    ("libstagefright/foundation/include/media/stagefright/foundation/ADebug.h",
        "media/module/foundation/include/media/stagefright/foundation/ADebug.h"),
    ("libstagefright/foundation/include/media/stagefright/foundation/AHandler.h",
        "media/module/foundation/include/media/stagefright/foundation/AHandler.h"),
    ("libstagefright/foundation/include/media/stagefright/foundation/ALooper.h",
        "media/module/foundation/include/media/stagefright/foundation/ALooper.h"),
    ("libstagefright/foundation/include/media/stagefright/foundation/ALooperRoster.h",
        "media/module/foundation/include/media/stagefright/foundation/ALooperRoster.h"),
    ("libstagefright/foundation/include/media/stagefright/foundation/AMessage.h",
        "media/module/foundation/include/media/stagefright/foundation/AMessage.h"),
    # AReplyToken.h does NOT exist as a separate header in android-16.0.0_r1
    # — its type definition was inlined into AMessage.h (see `struct AReplyToken`
    # inside AMessage.h around 2024). No fetch needed.
    ("libstagefright/foundation/include/media/stagefright/foundation/AString.h",
        "media/module/foundation/include/media/stagefright/foundation/AString.h"),
    ("libstagefright/foundation/include/media/stagefright/foundation/AUtils.h",
        "media/module/foundation/include/media/stagefright/foundation/AUtils.h"),
    ("libstagefright/foundation/include/media/stagefright/foundation/Mutexed.h",
        "media/module/foundation/include/media/stagefright/foundation/Mutexed.h"),
    ("libstagefright/foundation/include/media/stagefright/foundation/MediaDefs.h",
        "media/module/foundation/include/media/stagefright/foundation/MediaDefs.h"),
    ("libstagefright/foundation/include/media/stagefright/foundation/OpusHeader.h",
        "media/module/foundation/include/media/stagefright/foundation/OpusHeader.h"),
    ("libstagefright/foundation/include/media/stagefright/foundation/hexdump.h",
        "media/module/foundation/include/media/stagefright/foundation/hexdump.h"),
    ("libstagefright/foundation/ABuffer.cpp",       "media/module/foundation/ABuffer.cpp"),
    ("libstagefright/foundation/ADebug.cpp",        "media/module/foundation/ADebug.cpp"),
    ("libstagefright/foundation/AHandler.cpp",      "media/module/foundation/AHandler.cpp"),
    ("libstagefright/foundation/ALooper.cpp",       "media/module/foundation/ALooper.cpp"),
    ("libstagefright/foundation/ALooperRoster.cpp", "media/module/foundation/ALooperRoster.cpp"),
    ("libstagefright/foundation/AMessage.cpp",      "media/module/foundation/AMessage.cpp"),
    ("libstagefright/foundation/AString.cpp",       "media/module/foundation/AString.cpp"),
    ("libstagefright/foundation/MediaDefs.cpp",     "media/module/foundation/MediaDefs.cpp"),
    ("libstagefright/foundation/OpusHeader.cpp",    "media/module/foundation/OpusHeader.cpp"),
    ("libstagefright/foundation/hexdump.cpp",       "media/module/foundation/hexdump.cpp"),

    # ── FLAC: Codec2 component + libstagefright_flacdec wrapper (over system libFLAC) ─
    ("components/flac/C2SoftFlacDec.cpp", "av", "media/codec2/components/flac/C2SoftFlacDec.cpp"),
    ("components/flac/C2SoftFlacDec.h",   "av", "media/codec2/components/flac/C2SoftFlacDec.h"),
    ("codecs/flac/dec/FLACDecoder.cpp",   "av", "media/module/codecs/flac/dec/FLACDecoder.cpp"),
    ("codecs/flac/dec/FLACDecoder.h",     "av", "media/module/codecs/flac/dec/FLACDecoder.h"),

    # ── Vorbis: Codec2 component (av) + vendored Tremolo fixed-point lib (external/tremolo) ─
    ("components/vorbis/C2SoftVorbisDec.h",   "av", "media/codec2/components/vorbis/C2SoftVorbisDec.h"),
    ("components/vorbis/C2SoftVorbisDec.cpp", "av", "media/codec2/components/vorbis/C2SoftVorbisDec.cpp"),
    ("tremolo/Tremolo/bitwise.c",       "tremolo", "Tremolo/bitwise.c"),
    ("tremolo/Tremolo/framing.c",       "tremolo", "Tremolo/framing.c"),   # #included by bitwise.c (not compiled directly)
    ("tremolo/Tremolo/codebook.c",      "tremolo", "Tremolo/codebook.c"),
    ("tremolo/Tremolo/dsp.c",           "tremolo", "Tremolo/dsp.c"),
    ("tremolo/Tremolo/floor0.c",        "tremolo", "Tremolo/floor0.c"),
    ("tremolo/Tremolo/floor1.c",        "tremolo", "Tremolo/floor1.c"),
    ("tremolo/Tremolo/floor_lookup.c",  "tremolo", "Tremolo/floor_lookup.c"),
    ("tremolo/Tremolo/mapping0.c",      "tremolo", "Tremolo/mapping0.c"),
    ("tremolo/Tremolo/mdct.c",          "tremolo", "Tremolo/mdct.c"),
    ("tremolo/Tremolo/misc.c",          "tremolo", "Tremolo/misc.c"),
    ("tremolo/Tremolo/res012.c",        "tremolo", "Tremolo/res012.c"),
    ("tremolo/Tremolo/treminfo.c",      "tremolo", "Tremolo/treminfo.c"),
    ("tremolo/Tremolo/vorbisfile.c",    "tremolo", "Tremolo/vorbisfile.c"),
    ("tremolo/Tremolo/asm_arm.h",       "tremolo", "Tremolo/asm_arm.h"),
    ("tremolo/Tremolo/codebook.h",      "tremolo", "Tremolo/codebook.h"),
    ("tremolo/Tremolo/codec_internal.h","tremolo", "Tremolo/codec_internal.h"),
    ("tremolo/Tremolo/config_types.h",  "tremolo", "Tremolo/config_types.h"),
    ("tremolo/Tremolo/ivorbiscodec.h",  "tremolo", "Tremolo/ivorbiscodec.h"),
    ("tremolo/Tremolo/ivorbisfile.h",   "tremolo", "Tremolo/ivorbisfile.h"),
    ("tremolo/Tremolo/lsp_lookup.h",    "tremolo", "Tremolo/lsp_lookup.h"),
    ("tremolo/Tremolo/mdct.h",          "tremolo", "Tremolo/mdct.h"),
    ("tremolo/Tremolo/mdct_lookup.h",   "tremolo", "Tremolo/mdct_lookup.h"),
    ("tremolo/Tremolo/misc.h",          "tremolo", "Tremolo/misc.h"),
    ("tremolo/Tremolo/ogg.h",           "tremolo", "Tremolo/ogg.h"),
    ("tremolo/Tremolo/os.h",            "tremolo", "Tremolo/os.h"),
    ("tremolo/Tremolo/os_types.h",      "tremolo", "Tremolo/os_types.h"),
    ("tremolo/Tremolo/window_lookup.h", "tremolo", "Tremolo/window_lookup.h"),

    # ── MP3: Codec2 component + vendored PacketVideo pvmp3 decoder (libstagefright_mp3dec) ─
    ('components/mp3/C2SoftMp3Dec.h', 'av', 'media/codec2/components/mp3/C2SoftMp3Dec.h'),
    ('components/mp3/C2SoftMp3Dec.cpp', 'av', 'media/codec2/components/mp3/C2SoftMp3Dec.cpp'),
    ('codecs/mp3dec/include/pvmp3decoder_api.h', 'av', 'media/module/codecs/mp3dec/include/pvmp3decoder_api.h'),
    ('codecs/mp3dec/include/pvmp3_audio_type_defs.h', 'av', 'media/module/codecs/mp3dec/include/pvmp3_audio_type_defs.h'),
    ('codecs/mp3dec/include/mp3_decoder_selection.h', 'av', 'media/module/codecs/mp3dec/include/mp3_decoder_selection.h'),
    ('codecs/mp3dec/src/mp3_mem_funcs.h', 'av', 'media/module/codecs/mp3dec/src/mp3_mem_funcs.h'),
    ('codecs/mp3dec/src/pv_mp3_huffman.h', 'av', 'media/module/codecs/mp3dec/src/pv_mp3_huffman.h'),
    ('codecs/mp3dec/src/pv_mp3dec_fxd_op.h', 'av', 'media/module/codecs/mp3dec/src/pv_mp3dec_fxd_op.h'),
    ('codecs/mp3dec/src/pv_mp3dec_fxd_op_arm.h', 'av', 'media/module/codecs/mp3dec/src/pv_mp3dec_fxd_op_arm.h'),
    ('codecs/mp3dec/src/pv_mp3dec_fxd_op_arm_gcc.h', 'av', 'media/module/codecs/mp3dec/src/pv_mp3dec_fxd_op_arm_gcc.h'),
    ('codecs/mp3dec/src/pv_mp3dec_fxd_op_c_equivalent.h', 'av', 'media/module/codecs/mp3dec/src/pv_mp3dec_fxd_op_c_equivalent.h'),
    ('codecs/mp3dec/src/pv_mp3dec_fxd_op_msc_evc.h', 'av', 'media/module/codecs/mp3dec/src/pv_mp3dec_fxd_op_msc_evc.h'),
    ('codecs/mp3dec/src/pvmp3_dec_defs.h', 'av', 'media/module/codecs/mp3dec/src/pvmp3_dec_defs.h'),
    ('codecs/mp3dec/src/s_huffcodetab.h', 'av', 'media/module/codecs/mp3dec/src/s_huffcodetab.h'),
    ('codecs/mp3dec/src/s_mp3bits.h', 'av', 'media/module/codecs/mp3dec/src/s_mp3bits.h'),
    ('codecs/mp3dec/src/s_tmp3dec_chan.h', 'av', 'media/module/codecs/mp3dec/src/s_tmp3dec_chan.h'),
    ('codecs/mp3dec/src/s_tmp3dec_file.h', 'av', 'media/module/codecs/mp3dec/src/s_tmp3dec_file.h'),
    ('codecs/mp3dec/src/pvmp3_alias_reduction.h', 'av', 'media/module/codecs/mp3dec/src/pvmp3_alias_reduction.h'),
    ('codecs/mp3dec/src/pvmp3_crc.h', 'av', 'media/module/codecs/mp3dec/src/pvmp3_crc.h'),
    ('codecs/mp3dec/src/pvmp3_dct_16.h', 'av', 'media/module/codecs/mp3dec/src/pvmp3_dct_16.h'),
    ('codecs/mp3dec/src/pvmp3_decode_header.h', 'av', 'media/module/codecs/mp3dec/src/pvmp3_decode_header.h'),
    ('codecs/mp3dec/src/pvmp3_decode_huff_cw.h', 'av', 'media/module/codecs/mp3dec/src/pvmp3_decode_huff_cw.h'),
    ('codecs/mp3dec/src/pvmp3_dequantize_sample.h', 'av', 'media/module/codecs/mp3dec/src/pvmp3_dequantize_sample.h'),
    ('codecs/mp3dec/src/pvmp3_equalizer.h', 'av', 'media/module/codecs/mp3dec/src/pvmp3_equalizer.h'),
    ('codecs/mp3dec/src/pvmp3_framedecoder.h', 'av', 'media/module/codecs/mp3dec/src/pvmp3_framedecoder.h'),
    ('codecs/mp3dec/src/pvmp3_get_main_data_size.h', 'av', 'media/module/codecs/mp3dec/src/pvmp3_get_main_data_size.h'),
    ('codecs/mp3dec/src/pvmp3_get_scale_factors.h', 'av', 'media/module/codecs/mp3dec/src/pvmp3_get_scale_factors.h'),
    ('codecs/mp3dec/src/pvmp3_get_side_info.h', 'av', 'media/module/codecs/mp3dec/src/pvmp3_get_side_info.h'),
    ('codecs/mp3dec/src/pvmp3_getbits.h', 'av', 'media/module/codecs/mp3dec/src/pvmp3_getbits.h'),
    ('codecs/mp3dec/src/pvmp3_imdct_synth.h', 'av', 'media/module/codecs/mp3dec/src/pvmp3_imdct_synth.h'),
    ('codecs/mp3dec/src/pvmp3_mdct_18.h', 'av', 'media/module/codecs/mp3dec/src/pvmp3_mdct_18.h'),
    ('codecs/mp3dec/src/pvmp3_mdct_6.h', 'av', 'media/module/codecs/mp3dec/src/pvmp3_mdct_6.h'),
    ('codecs/mp3dec/src/pvmp3_mpeg2_get_scale_data.h', 'av', 'media/module/codecs/mp3dec/src/pvmp3_mpeg2_get_scale_data.h'),
    ('codecs/mp3dec/src/pvmp3_mpeg2_get_scale_factors.h', 'av', 'media/module/codecs/mp3dec/src/pvmp3_mpeg2_get_scale_factors.h'),
    ('codecs/mp3dec/src/pvmp3_mpeg2_stereo_proc.h', 'av', 'media/module/codecs/mp3dec/src/pvmp3_mpeg2_stereo_proc.h'),
    ('codecs/mp3dec/src/pvmp3_normalize.h', 'av', 'media/module/codecs/mp3dec/src/pvmp3_normalize.h'),
    ('codecs/mp3dec/src/pvmp3_poly_phase_synthesis.h', 'av', 'media/module/codecs/mp3dec/src/pvmp3_poly_phase_synthesis.h'),
    ('codecs/mp3dec/src/pvmp3_polyphase_filter_window.h', 'av', 'media/module/codecs/mp3dec/src/pvmp3_polyphase_filter_window.h'),
    ('codecs/mp3dec/src/pvmp3_reorder.h', 'av', 'media/module/codecs/mp3dec/src/pvmp3_reorder.h'),
    ('codecs/mp3dec/src/pvmp3_seek_synch.h', 'av', 'media/module/codecs/mp3dec/src/pvmp3_seek_synch.h'),
    ('codecs/mp3dec/src/pvmp3_stereo_proc.h', 'av', 'media/module/codecs/mp3dec/src/pvmp3_stereo_proc.h'),
    ('codecs/mp3dec/src/pvmp3_tables.h', 'av', 'media/module/codecs/mp3dec/src/pvmp3_tables.h'),
    ('codecs/mp3dec/src/pvmp3_normalize.cpp', 'av', 'media/module/codecs/mp3dec/src/pvmp3_normalize.cpp'),
    ('codecs/mp3dec/src/pvmp3_alias_reduction.cpp', 'av', 'media/module/codecs/mp3dec/src/pvmp3_alias_reduction.cpp'),
    ('codecs/mp3dec/src/pvmp3_crc.cpp', 'av', 'media/module/codecs/mp3dec/src/pvmp3_crc.cpp'),
    ('codecs/mp3dec/src/pvmp3_decode_header.cpp', 'av', 'media/module/codecs/mp3dec/src/pvmp3_decode_header.cpp'),
    ('codecs/mp3dec/src/pvmp3_decode_huff_cw.cpp', 'av', 'media/module/codecs/mp3dec/src/pvmp3_decode_huff_cw.cpp'),
    ('codecs/mp3dec/src/pvmp3_getbits.cpp', 'av', 'media/module/codecs/mp3dec/src/pvmp3_getbits.cpp'),
    ('codecs/mp3dec/src/pvmp3_dequantize_sample.cpp', 'av', 'media/module/codecs/mp3dec/src/pvmp3_dequantize_sample.cpp'),
    ('codecs/mp3dec/src/pvmp3_framedecoder.cpp', 'av', 'media/module/codecs/mp3dec/src/pvmp3_framedecoder.cpp'),
    ('codecs/mp3dec/src/pvmp3_get_main_data_size.cpp', 'av', 'media/module/codecs/mp3dec/src/pvmp3_get_main_data_size.cpp'),
    ('codecs/mp3dec/src/pvmp3_get_side_info.cpp', 'av', 'media/module/codecs/mp3dec/src/pvmp3_get_side_info.cpp'),
    ('codecs/mp3dec/src/pvmp3_get_scale_factors.cpp', 'av', 'media/module/codecs/mp3dec/src/pvmp3_get_scale_factors.cpp'),
    ('codecs/mp3dec/src/pvmp3_mpeg2_get_scale_data.cpp', 'av', 'media/module/codecs/mp3dec/src/pvmp3_mpeg2_get_scale_data.cpp'),
    ('codecs/mp3dec/src/pvmp3_mpeg2_get_scale_factors.cpp', 'av', 'media/module/codecs/mp3dec/src/pvmp3_mpeg2_get_scale_factors.cpp'),
    ('codecs/mp3dec/src/pvmp3_mpeg2_stereo_proc.cpp', 'av', 'media/module/codecs/mp3dec/src/pvmp3_mpeg2_stereo_proc.cpp'),
    ('codecs/mp3dec/src/pvmp3_huffman_decoding.cpp', 'av', 'media/module/codecs/mp3dec/src/pvmp3_huffman_decoding.cpp'),
    ('codecs/mp3dec/src/pvmp3_huffman_parsing.cpp', 'av', 'media/module/codecs/mp3dec/src/pvmp3_huffman_parsing.cpp'),
    ('codecs/mp3dec/src/pvmp3_tables.cpp', 'av', 'media/module/codecs/mp3dec/src/pvmp3_tables.cpp'),
    ('codecs/mp3dec/src/pvmp3_imdct_synth.cpp', 'av', 'media/module/codecs/mp3dec/src/pvmp3_imdct_synth.cpp'),
    ('codecs/mp3dec/src/pvmp3_mdct_6.cpp', 'av', 'media/module/codecs/mp3dec/src/pvmp3_mdct_6.cpp'),
    ('codecs/mp3dec/src/pvmp3_dct_6.cpp', 'av', 'media/module/codecs/mp3dec/src/pvmp3_dct_6.cpp'),
    ('codecs/mp3dec/src/pvmp3_poly_phase_synthesis.cpp', 'av', 'media/module/codecs/mp3dec/src/pvmp3_poly_phase_synthesis.cpp'),
    ('codecs/mp3dec/src/pvmp3_equalizer.cpp', 'av', 'media/module/codecs/mp3dec/src/pvmp3_equalizer.cpp'),
    ('codecs/mp3dec/src/pvmp3_seek_synch.cpp', 'av', 'media/module/codecs/mp3dec/src/pvmp3_seek_synch.cpp'),
    ('codecs/mp3dec/src/pvmp3_stereo_proc.cpp', 'av', 'media/module/codecs/mp3dec/src/pvmp3_stereo_proc.cpp'),
    ('codecs/mp3dec/src/pvmp3_reorder.cpp', 'av', 'media/module/codecs/mp3dec/src/pvmp3_reorder.cpp'),
    ('codecs/mp3dec/src/pvmp3_polyphase_filter_window.cpp', 'av', 'media/module/codecs/mp3dec/src/pvmp3_polyphase_filter_window.cpp'),
    ('codecs/mp3dec/src/pvmp3_mdct_18.cpp', 'av', 'media/module/codecs/mp3dec/src/pvmp3_mdct_18.cpp'),
    ('codecs/mp3dec/src/pvmp3_dct_9.cpp', 'av', 'media/module/codecs/mp3dec/src/pvmp3_dct_9.cpp'),
    ('codecs/mp3dec/src/pvmp3_dct_16.cpp', 'av', 'media/module/codecs/mp3dec/src/pvmp3_dct_16.cpp'),
]


_USER_AGENT = "gst-plugins-android/fetch_aosp_sources (+https://github.com/changyongahn/PoC_GstAnd)"


def _fetch_one(url: str, dest: Path, force: bool, max_retries: int = 6) -> tuple[Path, int, str | None]:
    """Fetch one gitiles ?format=TEXT URL with exponential backoff on 429/5xx.

    Idempotent: a non-empty file already at `dest` short-circuits unless --force.
    """
    if dest.exists() and dest.stat().st_size > 0 and not force:
        return dest, dest.stat().st_size, None
    dest.parent.mkdir(parents=True, exist_ok=True)

    last_err: str | None = None
    for attempt in range(max_retries):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": _USER_AGENT})
            with urllib.request.urlopen(req, timeout=30) as resp:
                body = resp.read()
            try:
                raw = base64.b64decode(body)
            except Exception as e:
                return dest, 0, f"base64 decode failed for {url}: {e}"
            dest.write_bytes(raw)
            return dest, len(raw), None
        except urllib.error.HTTPError as e:
            last_err = f"HTTP {e.code} on {url}"
            # Only 404 is fatal — the URL is genuinely wrong, retrying won't fix it.
            if e.code == 404:
                return dest, 0, last_err
            # 429 (rate limit) and 5xx — back off and retry.
        except (urllib.error.URLError, TimeoutError) as e:
            last_err = f"{e} on {url}"
        # Exponential backoff with jitter: 1, 2, 4, 8, 16, 32 seconds (plus 0-1s jitter).
        sleep_s = (2 ** attempt) + random.random()
        time.sleep(sleep_s)
    return dest, 0, last_err or f"GET {url} failed after {max_retries} retries"


def _from_tarball(tarball: Path, out: Path, files: list[tuple[str, str]]) -> int:
    n_copied = 0
    with tarfile.open(tarball) as tf:
        members = {m.name.lstrip("./"): m for m in tf.getmembers()}
        for entry in files:
            dest_rel, _repo, aosp_rel = _normalize(entry)
            m = members.get(aosp_rel) or members.get(f"frameworks/av/{aosp_rel}")
            if not m:
                continue
            target = out / dest_rel
            target.parent.mkdir(parents=True, exist_ok=True)
            f = tf.extractfile(m)
            if f is None:
                continue
            target.write_bytes(f.read())
            n_copied += 1
    return n_copied


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--out", required=True, type=Path,
                   help="Destination root (e.g. aosp/).")
    p.add_argument("--tag", default=DEFAULT_TAG,
                   help=f"AOSP tag to fetch from (default {DEFAULT_TAG}).")
    p.add_argument("--force", action="store_true",
                   help="Re-download even if dest exists.")
    p.add_argument("--jobs", type=int, default=4,
                   help="Parallel download jobs (default 4 — keep low to avoid gitiles 429s).")
    p.add_argument("--tarball-mirror", type=Path, default=None,
                   help="Pre-downloaded frameworks/av tarball; if given, skip network.")
    p.add_argument("--manifest", type=Path, default=None,
                   help="Optional path to write a sha256 manifest of the fetched tree.")
    args = p.parse_args(argv)

    out: Path = args.out
    out.mkdir(parents=True, exist_ok=True)

    print(f"[fetch_aosp_sources] tag={args.tag} out={out} jobs={args.jobs}", file=sys.stderr)

    n_total = len(FILES)
    n_ok = 0
    failures: list[str] = []

    if args.tarball_mirror:
        if not args.tarball_mirror.exists():
            print(f"--tarball-mirror not found: {args.tarball_mirror}", file=sys.stderr)
            return 2
        n_ok = _from_tarball(args.tarball_mirror, out, FILES)
        if n_ok != n_total:
            failures.append(f"tarball mirror only had {n_ok}/{n_total} files")
    else:
        plan = []
        for entry in FILES:
            d, repo, p = _normalize(entry)
            url = REPO_TEMPLATES[repo].format(tag=args.tag, path=p)
            plan.append((url, out / d))
        with futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            for dest, size, err in pool.map(lambda x: _fetch_one(x[0], x[1], args.force), plan):
                if err:
                    failures.append(err)
                else:
                    n_ok += 1

    print(f"[fetch_aosp_sources] {n_ok}/{n_total} files present (size>0).", file=sys.stderr)

    if failures:
        for f in failures[:20]:
            print(f"  FAIL: {f}", file=sys.stderr)
        if len(failures) > 20:
            print(f"  ... ({len(failures) - 20} more)", file=sys.stderr)
        return 1

    if args.manifest:
        h = hashlib.sha256()
        for entry in FILES:
            dest_rel = _normalize(entry)[0]
            data = (out / dest_rel).read_bytes()
            h.update(dest_rel.encode())
            h.update(b"\0")
            h.update(data)
        args.manifest.write_text(h.hexdigest() + "\n")

    # Apply the small mechanical gcc/glibc compatibility patches to the freshly
    # fetched tree. Kept in a separate module so they're auditable + idempotent.
    try:
        from apply_local_patches import apply as _apply_local_patches
    except ImportError:
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from apply_local_patches import apply as _apply_local_patches
    _apply_local_patches(out)

    (out / ".fetch.stamp").write_text(f"{args.tag}\n{n_ok}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
