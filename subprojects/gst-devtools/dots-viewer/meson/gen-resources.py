#!/usr/bin/env python3
"""Generate the embedded static-asset table, the Meson replacement for build.rs.

Meson's cargo module never runs build.rs, so this reproduces the `generated.rs`
that the `static-files` crate's `resource_dir("./static").build()` emits for a
plain `cargo` build. `src/main.rs` includes it and serves it through
`actix_web_static_files::ResourceFiles`.
"""

import json
import os
import sys


def rs(s: str) -> str:
    # Rust's {:?}/string literals match a JSON string for ASCII content.
    return json.dumps(s)


MIME_TYPES = {
    "css": "text/css",
    "html": "text/html",
    "jpeg": "image/jpeg",
    "jpg": "image/jpeg",
    "js": "text/javascript",
    "json": "application/json",
    "png": "image/png",
    "svg": "image/svg+xml",
}


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <static-dir> <generated.rs>", file=sys.stderr)
        return 1

    static_dir = os.path.realpath(sys.argv[1])
    output = sys.argv[2]

    files = []
    for root, _dirs, names in os.walk(static_dir):
        for name in names:
            abs_path = os.path.realpath(os.path.join(root, name))
            key = os.path.relpath(abs_path, static_dir).replace(os.sep, "/")
            files.append((key, abs_path))
    files.sort()

    lines = [
        "#[allow(clippy::unreadable_literal)] #[must_use] pub fn generate() "
        "-> ::std::collections::HashMap<&'static str, ::static_files::Resource> {",
        "use ::static_files::resource::new_resource as n;",
        "use ::std::include_bytes as i;",
        "let mut r = ::std::collections::HashMap::new();",
    ]
    for key, abs_path in files:
        ext = key.rsplit(".", 1)[-1].lower() if "." in key else ""
        mime = MIME_TYPES.get(ext, "application/octet-stream")
        lines.append(
            f"r.insert({rs(key)}, n(i!({rs(abs_path)}), 0, {rs(mime)}));"
        )
    lines.append("r")
    lines.append("}")

    with open(output, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
