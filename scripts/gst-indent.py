#!/usr/bin/env python3

import os
import sys
import shutil
import textwrap
import subprocess

if __name__ == '__main__':
    git_clang_format = shutil.which('git-clang-format')
    if not git_clang_format:
        # This can only happen if the user uses this script outside of pre-commit hook.
        print(textwrap.dedent('''\
            WARNING: git-clang-format not found.
            It is recommended to use latest version:
                pip install clang-format
            '''), file=sys.stderr)
        sys.exit(1)

    # On CI we want to check the merge request diff instead of staged changes.
    base_sha = os.environ.get('CI_MERGE_REQUEST_DIFF_BASE_SHA')
    args = [base_sha] if base_sha else sys.argv[1:]

    try:
        subprocess.check_call([git_clang_format, '--style', 'file', '-f', '--extensions', 'c,cpp'] + args)
    except subprocess.CalledProcessError as e:
        sys.exit(1)
