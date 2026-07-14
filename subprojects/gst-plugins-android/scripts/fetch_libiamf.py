#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Fetch AOM libiamf (the IAMF reference decoder) into the subproject.

Unlike the AOSP fetcher (which pulls individual files from android.googlesource
gitiles), libiamf is a self-contained git repo on GitHub, so we just do a
shallow clone of a specific *release tag* and drop a stamp file.

We deliberately pin a TAG (default v1.0.1), NOT main/HEAD:
  * v1.1.0+/HEAD pull the PRIVATE `oar-private` submodule (binaural renderer,
    github 404 for non-members) and require cmake >= 3.28.
  * v1.0.1 has no submodules, builds with cmake >= 3.6, links system
    opus/fdk-aac/FLAC, and ships test vectors. It is fully buildable from
    public source and is what our meson glue (iamf/meson.build) expects.

The clone lands at <out>/code/{src,include,dep_external,...} — i.e. the repo's
`code/` directory is the library root. We do NOT keep the .git dir.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

REPO = "https://github.com/AOMediaCodec/libiamf.git"
DEFAULT_TAG = "v1.0.1"
STAMP = ".fetch.stamp"

# Only these top-level entries of the repo are needed to build the decoder and
# keep the verification reference handy. Everything else (CI, docs, the MP4
# demux tool's build files) is dropped to keep the vendored tree lean.
KEEP = ["code", "LICENSE", "PATENTS"]


def log(msg: str) -> None:
    print(f"[fetch_libiamf] {msg}", file=sys.stderr)


def main() -> int:
    ap = argparse.ArgumentParser(description="Fetch AOM libiamf into the subproject.")
    ap.add_argument("--out", required=True, help="Destination dir (gitignored).")
    ap.add_argument("--tag", default=DEFAULT_TAG, help=f"git tag (default {DEFAULT_TAG}).")
    ap.add_argument("--repo", default=REPO, help="git URL override.")
    args = ap.parse_args()

    out = os.path.abspath(args.out)
    stamp = os.path.join(out, STAMP)
    if os.path.exists(stamp):
        log(f"already present ({out}); skipping.")
        return 0

    if shutil.which("git") is None:
        log("ERROR: git not found on PATH.")
        return 2

    os.makedirs(out, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="libiamf-clone-") as tmp:
        clone = os.path.join(tmp, "libiamf")
        cmd = ["git", "clone", "--depth", "1", "--branch", args.tag,
               "--single-branch", args.repo, clone]
        log("running: " + " ".join(cmd))
        r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           text=True)
        if r.returncode != 0:
            log("git clone failed:\n" + (r.stdout or ""))
            return r.returncode

        # Copy only the entries we keep into <out> (clone has no submodules at
        # this tag, so a plain copy is complete).
        for name in KEEP:
            src = os.path.join(clone, name)
            if not os.path.exists(src):
                log(f"WARNING: expected entry '{name}' missing from clone.")
                continue
            dst = os.path.join(out, name)
            if os.path.isdir(src):
                if os.path.exists(dst):
                    shutil.rmtree(dst)
                shutil.copytree(src, dst)
            else:
                shutil.copy2(src, dst)

    # Sanity: the decoder's public header must be where iamf/meson.build expects.
    pub = os.path.join(out, "code", "include", "IAMF_decoder.h")
    if not os.path.isfile(pub):
        log(f"ERROR: post-fetch sanity check failed (missing {pub}).")
        return 3

    with open(stamp, "w") as fh:
        fh.write(f"libiamf {args.tag}\n")
    log(f"OK: libiamf {args.tag} staged at {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
