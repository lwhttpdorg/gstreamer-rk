#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Apply small, mechanical, reproducible patches to the vendored AOSP tree.

We do NOT hand-edit the fetched AOSP sources — instead every necessary tweak
lives here as an idempotent (old -> new) string replacement, so a re-fetch +
re-patch always yields the same tree and the diff from upstream is auditable.

Each patch records WHY it is needed. The two classes of fix:
  - GCC-vs-clang divergences (AOSP is built with clang; we use gcc).
  - bionic-vs-glibc divergences.

Run automatically at the tail of fetch_aosp_sources.py, or standalone:
    scripts/apply_local_patches.py --root aosp
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

# (relative_path, old, new, why). Applied in order; idempotent (if `old` is
# absent but `new` is present, the patch is considered already applied).
PATCHES = [
    # ── C2Config.h: nested-enum enumerators referenced unqualified ──────────
    # supplemental_info_t / resource_kind_t are forward-declared inside
    # `struct C2Config` and defined out-of-line via C2ENUM. Their enumerators
    # (INFO_NONE, CONST) therefore live in C2Config's scope, but the structs
    # C2SupplementalDataStruct / C2SystemResourceStruct sit at namespace scope
    # and reference them unqualified. clang resolves this; gcc does not. Qualify.
    (
        "core/include/C2Config.h",
        ": type_(INFO_NONE) { }",
        ": type_(C2Config::INFO_NONE) { }",
        "gcc: qualify nested-enum enumerator INFO_NONE (clang-vs-gcc lookup)",
    ),
    (
        "core/include/C2Config.h",
        "C2SystemResourceStruct() : C2SystemResourceStruct(0, CONST, 0) {}",
        "C2SystemResourceStruct() : C2SystemResourceStruct(0, C2Config::CONST, 0) {}",
        "gcc: qualify nested-enum enumerator CONST (clang-vs-gcc lookup)",
    ),

    # ── C2Config.h: enum initializers referencing sibling enumerators ───────
    # The C2ENUM reflection helper re-emits each enumerator's initializer
    # expression at namespace scope (inside C2FieldDescriptor::namedValuesFor),
    # where an unqualified sibling enumerator like INFO_PREFIX_SEI_UNIT is not
    # visible under gcc. Qualifying with C2Config:: resolves in BOTH the enum
    # definition and the reflection helper.
    # Qualify with the FULL enum-type scope (not just C2Config::): gcc does not
    # treat an out-of-line-defined nested enum's enumerators as members of the
    # enclosing struct (C2Config::INFO_PREFIX_SEI_UNIT fails), but it does
    # accept them via the enum type itself, which is also valid at the
    # namespace-scope reflection helper.
    (
        "core/include/C2Config.h",
        "INFO_SEI_USER_DATA = INFO_PREFIX_SEI_UNIT | 4,",
        "INFO_SEI_USER_DATA = C2Config::supplemental_info_t::INFO_PREFIX_SEI_UNIT | 4,",
        "gcc: qualify INFO_PREFIX_SEI_UNIT via enum type scope",
    ),
    (
        "core/include/C2Config.h",
        "INFO_SEI_MDCV      = INFO_PREFIX_SEI_UNIT | 137,",
        "INFO_SEI_MDCV      = C2Config::supplemental_info_t::INFO_PREFIX_SEI_UNIT | 137,",
        "gcc: qualify INFO_PREFIX_SEI_UNIT via enum type scope",
    ),
    (
        "core/include/C2Config.h",
        "INFO_SET_USER_DATA_SFX = INFO_SUFFIX_SEI_UNIT | 4,",
        "INFO_SET_USER_DATA_SFX = C2Config::supplemental_info_t::INFO_SUFFIX_SEI_UNIT | 4,",
        "gcc: qualify INFO_SUFFIX_SEI_UNIT via enum type scope",
    ),

    # ── C2Buffer.h: C2Handle must be native_handle_t, not void* ─────────────
    # The non-Android fallback typedefs C2Handle to void*, but C2Buffer.cpp's
    # own handle plumbing (priorLinearAllocation(cHandle), CreateLinearBlock)
    # assumes C2Handle == native_handle_t (as it is on Android via
    # android-C2Buffer.h). We have a native_handle_t shim, so use it — this
    # makes `const C2Handle*` == `const native_handle_t*` and the internal
    # conversions type-check exactly as on Android.
    (
        "core/include/C2Buffer.h",
        "#else\n\ntypedef void* C2Handle;\n\n#endif",
        "#else\n\n#include <cutils/native_handle.h>\ntypedef native_handle_t C2Handle;\n\n#endif",
        "linux: C2Handle = native_handle_t (matches C2Buffer.cpp handle plumbing)",
    ),

    # ── C2ParamDef.h: flexible array as sole struct member ──────────────────
    # gcc rejects `T value[];` (C99 flexible array) when it is the ONLY data
    # member of a struct (C2SimpleValueStruct<T[]>), with no flag to relax it
    # (clang accepts it). The fix must be LAYOUT-NEUTRAL: C2ParamDef.h asserts
    # `sizeof(struct) == BASE_SIZE` and derives flexCount from sizeof, so a real
    # padding byte would corrupt the flex protocol. A `[[no_unique_address]]`
    # member of an EMPTY type makes the struct "not otherwise empty" (so gcc
    # accepts the flex array) yet collapses to zero size — sizeof and
    # offsetof(value) are unchanged, and decltype(value) stays `T[]` so the
    # _C2FlexHelper flexible-type detection still fires.
    (
        "core/include/C2ParamDef.h",
        '                  "C2SimpleValueStruct<T[]> is only for BLOB or STRING");\n    T value[];',
        '                  "C2SimpleValueStruct<T[]> is only for BLOB or STRING");\n'
        '    struct _GstC2FlexPad {};  /* empty */\n'
        '    [[no_unique_address]] _GstC2FlexPad _gst_c2_flex_pad;  /* gcc: flex array may not be sole member; empty+[[no_unique_address]] is layout-neutral */\n'
        '    T value[];',
        "gcc: zero-size [[no_unique_address]] member so flex array is not the sole member",
    ),

    # ── C2ParamDef.h: make the flex-size math key off BASE_SIZE, not sizeof ──
    # clang gives a flexible-array-only struct sizeof 0; gcc gives 1 (a struct
    # can't be zero-size), and the enclosing param then pads (e.g. 8+1 -> 12),
    # so `sizeof(_Type) == BASE_SIZE` is false AND flexCount = (size -
    # sizeof(_Type))/FLEX would be wrong by the padding. BASE_SIZE
    # (= sizeof(S)+sizeof(C2Param)) is what the ALLOCATOR (CalcSize) already
    # uses, so keying flexCount/setFlexCount off BASE_SIZE too makes the
    # round-trip self-consistent regardless of the gcc/clang sizeof divergence.
    # For every other (non-FAM-sole-member) flex struct, sizeof(_Type) ==
    # BASE_SIZE already, so this is a no-op there.
    (
        "core/include/C2ParamDef.h",
        'static_assert(sizeof(_Type) == _Type::BASE_SIZE, "incorrect BASE_SIZE");',
        'static_assert(sizeof(_Type) >= _Type::BASE_SIZE, "incorrect BASE_SIZE");',
        "gcc: relax BASE_SIZE assert (clang FAM sizeof 0 vs gcc 1 padding)",
    ),
    (
        "core/include/C2ParamDef.h",
        "if (sz >= sizeof(_Type)) { \\\n            return (sz - sizeof(_Type)) / _Type::FLEX_SIZE; \\",
        "if (sz >= _Type::BASE_SIZE) { \\\n            return (sz - _Type::BASE_SIZE) / _Type::FLEX_SIZE; \\",
        "gcc: flexCount keyed off BASE_SIZE for layout consistency",
    ),
    (
        "core/include/C2ParamDef.h",
        "this->setSize(sizeof(_Type) + _Type::FLEX_SIZE * count);",
        "this->setSize(_Type::BASE_SIZE + _Type::FLEX_SIZE * count);",
        "gcc: setFlexCount keyed off BASE_SIZE for layout consistency",
    ),

    # ── C2ParamDef.h: C2SimpleArrayStruct<T> — same FAM-sole-member fix ──────
    # The array (non-string) flex struct has `T values[];` as its sole data
    # member too; apply the identical layout-neutral [[no_unique_address]]
    # sentinel so gcc accepts it. decltype(values) stays T[] for FLEX detection.
    (
        "core/include/C2ParamDef.h",
        "    T values[]; ///< array member",
        "    struct _GstC2ArrPad {};  /* empty */\n"
        "    [[no_unique_address]] _GstC2ArrPad _gst_c2_arr_pad;  /* gcc: flex array may not be sole member */\n"
        "    T values[]; ///< array member",
        "gcc: zero-size sentinel so C2SimpleArrayStruct flex array is not sole member",
    ),

    # ── C2SoftAacDec.cpp: FDK transport type for upstream fdk-aac ───────────
    # AOSP opens the decoder with TT_MP4_ADIF; for raw-AAC (the dominant MP4
    # case, and the path we drive) it then calls aacDecoder_ConfigRaw(ASC) and
    # feeds bare access units. We open with TT_MP4_RAW instead — the canonical
    # upstream-fdk-aac configuration for ConfigRaw + raw-AU feeding (what the
    # stock GStreamer fdkaacdec uses). We convert ADTS->raw upstream via
    # aacparse (c2aacdec advertises stream-format=raw only), so the decoder
    # always receives an ASC + raw AUs and never needs ADTS/ADIF transport sync.
    (
        "components/aac/C2SoftAacDec.cpp",
        "aacDecoder_Open(TT_MP4_ADIF, /* num layers */ 1)",
        "aacDecoder_Open(TT_MP4_RAW, /* num layers */ 1)  /* gst-c2: raw-AU + ConfigRaw path for upstream fdk-aac */",
        "fdk-aac: open TT_MP4_RAW for the ConfigRaw + raw access-unit path",
    ),

    # ── SimpleC2Interface.cpp: unqualified api_feature_t enumerators ─────────
    # Same nested-enum-scope divergence as C2Config.h, but in a .cpp: the
    # C2Config::api_feature_t enumerators are referenced unqualified. Qualify
    # with the enum type scope so gcc resolves them.
    (
        "components/base/SimpleC2Interface.cpp",
        "                    API_REFLECTION |\n"
        "                    API_VALUES |\n"
        "                    API_CURRENT_VALUES |\n"
        "                    API_DEPENDENCY |\n"
        "                    API_SAME_INPUT_BUFFER)))",
        "                    C2Config::api_feature_t::API_REFLECTION |\n"
        "                    C2Config::api_feature_t::API_VALUES |\n"
        "                    C2Config::api_feature_t::API_CURRENT_VALUES |\n"
        "                    C2Config::api_feature_t::API_DEPENDENCY |\n"
        "                    C2Config::api_feature_t::API_SAME_INPUT_BUFFER)))",
        "gcc: qualify api_feature_t enumerators via enum type scope",
    ),
]


def apply(root: Path, verbose: bool = True) -> int:
    applied = skipped = 0
    failures: list[str] = []
    for rel, old, new, why in PATCHES:
        f = root / rel
        if not f.exists():
            failures.append(f"missing file {rel} (fetch first)")
            continue
        text = f.read_text()
        if new in text:
            skipped += 1
            continue
        if old not in text:
            failures.append(f"{rel}: anchor not found for [{why}]")
            continue
        f.write_text(text.replace(old, new))
        applied += 1
        if verbose:
            print(f"  patched {rel}: {why}", file=sys.stderr)
    if verbose:
        print(f"[apply_local_patches] {applied} applied, {skipped} already-applied, "
              f"{len(failures)} failed", file=sys.stderr)
    for fmsg in failures:
        print(f"  WARN: {fmsg}", file=sys.stderr)
    # Anchor-not-found is only fatal if NOTHING matched (likely an AOSP tag
    # change); a single stale anchor among many is surfaced as a warning.
    return 1 if applied == 0 and skipped == 0 and failures else 0


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", type=Path, default=Path("aosp"),
                   help="Vendored AOSP root (default ./aosp).")
    args = p.parse_args(argv)
    return apply(args.root)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
