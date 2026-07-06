#!/usr/bin/env python3
"""Run mime_guess's own build.rs to regenerate src/mime_types_generated.rs.

Meson does not run build.rs, and on stable rustc it cannot pass OUT_DIR, so
instead of reimplementing build.rs we compile and run it: the output is then
faithful by construction (it *is* build.rs), driven by the same Cargo features
Meson resolved. build.rs's only dependency is `unicase` (a single, zero-dep
crate); we compile that first, then build.rs against it, run it with OUT_DIR
pointing at a temp dir, and copy the result next to the source where the
(redirected) relative include! picks it up.

The phf feature additionally needs the phf_codegen dependency tree and is not
supported here; we fail loudly if it is enabled.
"""

import argparse
import glob
import os
import subprocess
import sys
import tarfile
import tempfile


def find_unicase_src(subprojects_dir: str, tmp: str) -> str:
    """Return a directory containing unicase's src/lib.rs.

    Prefer the already-extracted subproject; fall back to extracting the
    packagecache tarball (the sibling may not be unpacked yet when this hook
    runs during configuration)."""
    hits = glob.glob(os.path.join(subprojects_dir, "unicase-*", "src", "lib.rs"))
    if hits:
        return os.path.dirname(os.path.dirname(hits[0]))
    tarballs = glob.glob(os.path.join(subprojects_dir, "packagecache", "unicase-*.tar.gz"))
    if not tarballs:
        raise SystemExit("run-build-rs: cannot find unicase source or tarball")
    with tarfile.open(tarballs[0]) as t:
        t.extractall(tmp)
    hits = glob.glob(os.path.join(tmp, "unicase-*", "src", "lib.rs"))
    if not hits:
        raise SystemExit("run-build-rs: unicase tarball has no src/lib.rs")
    return os.path.dirname(os.path.dirname(hits[0]))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--crate-root", required=True)  # has build.rs + src/
    ap.add_argument("--subprojects-dir", required=True)  # for unicase
    ap.add_argument("--out", required=True)  # dest mime_types_generated.rs
    ap.add_argument("--feature", action="append", default=[])  # resolved cargo features
    # The rust compiler command (Meson's get_compiler('rust').cmd_array()); a
    # positional after `--` so its own flags (e.g. `-C linker=cc`) are not parsed
    # as our options.
    ap.add_argument("rustc", nargs="+")
    args = ap.parse_args()

    if "phf" in args.feature:
        print("run-build-rs: the 'phf' feature is not supported (needs the "
              "phf_codegen dependency tree); regenerate with cargo for phf",
              file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory() as tmp:
        unicase_src = find_unicase_src(args.subprojects_dir, tmp)
        # unicase: edition 2018, no build.rs, no deps.
        subprocess.run(
            [*args.rustc, "--edition", "2018", "--crate-type", "lib",
             "--crate-name", "unicase", os.path.join(unicase_src, "src", "lib.rs"),
             "--out-dir", tmp],
            check=True,
        )
        # build.rs: edition 2015 (mime_guess sets none); let it decide what to
        # emit from the resolved features (build_rev_map is cfg(rev-mappings)).
        build_rs = os.path.join(args.crate_root, "build.rs")
        cmd = [*args.rustc, build_rs, "--extern",
               "unicase=" + os.path.join(tmp, "libunicase.rlib"), "-L", tmp,
               "--crate-name", "build_script_build", "--out-dir", tmp]
        for f in args.feature:
            cmd += ["--cfg", 'feature="%s"' % f]
        subprocess.run(cmd, check=True)
        subprocess.run([os.path.join(tmp, "build_script_build")],
                       check=True, env={**os.environ, "OUT_DIR": tmp})
        with open(os.path.join(tmp, "mime_types_generated.rs"), "rb") as f:
            generated = f.read()

    with open(args.out, "wb") as f:
        f.write(generated)
    return 0


if __name__ == "__main__":
    sys.exit(main())
