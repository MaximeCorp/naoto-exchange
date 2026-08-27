#!/usr/bin/env python3
"""
fix_include_case.py

Companion to rename_to_snake_case.py. Scans source files for #include
directives — both quoted ("...") and angle-bracket (<...>) — and
rewrites the filename portion from camelCase/PascalCase to snake_case,
using the exact same conversion rule as the rename script, so running
both scripts together always produces consistent results.

No distinction is made between quoted and angle-bracket includes —
any include whose filename is already snake_case (e.g. <vector>,
<thread>) is left untouched automatically, since the conversion is a
no-op on those. Only includes with a camelCase/PascalCase filename
get rewritten.

CAUTION: if any vendored/third-party header genuinely has a
camelCase filename (uncommon, but possible) and its #include line
gets rewritten here without the actual file also being renamed, the
build will break on a missing-file error. Run with a plain diff/dry
run first and skim the output before applying, and re-run
rename_to_snake_case.py on any third_party paths you also want kept
in sync — or just fix the file if the error shows up, it's a fast fix.

Examples rewritten inside a file's #include lines:
    #include "EventHandlerBase.hpp"             -> #include "event_handler_base.hpp"
    #include <marketplace/common/orderRequest.hpp>
        -> #include <marketplace/common/order_request.hpp>
    #include <vector>                            -> unchanged (already snake_case)

Only the final path component (the filename) is converted — directory
names in the include path are left as-is, since directory naming is a
separate, already-settled convention (lowercase folders).

Usage:
    python3 fix_include_case.py <root_dir> [--extensions hpp,h,cpp] [--apply]

By default this does a DRY RUN and only prints what it would change.
Pass --apply to actually rewrite the files in place.
"""

import argparse
import re
import sys
from pathlib import Path


# Matches both quoted and angle-bracket includes in one pass.
INCLUDE_RE = re.compile(r'(#include\s*[<"])([^">]+)([>"])')


def to_snake_case(stem: str) -> str:
    """Identical conversion rule to rename_to_snake_case.py — keep in sync."""
    s1 = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", stem)
    s2 = re.sub(r"(?<=[A-Z])(?=[A-Z][a-z])", "_", s1)
    return s2.lower()


def convert_include_path(path: str) -> str:
    """
    Convert only the filename portion of an include path.
    'marketplace/common/orderRequest.hpp' -> 'marketplace/common/order_request.hpp'
    """
    p = Path(path)
    new_stem = to_snake_case(p.stem)
    if new_stem == p.stem:
        return path  # already snake_case, no change
    new_name = f"{new_stem}{p.suffix}"
    if p.parent == Path("."):
        return new_name
    return str(p.parent / new_name)


def find_target_files(root: Path, extensions: set[str]) -> list[Path]:
    files = []
    for ext in extensions:
        files.extend(root.rglob(f"*.{ext}"))
    return sorted(set(files))


def process_file(path: Path):
    """
    Returns (changed: bool, new_text: str, changes: list[(old, new)])
    without writing anything — caller decides whether to apply.
    """
    text = path.read_text(encoding="utf-8")
    changes = []

    def replacer(match: re.Match) -> str:
        prefix, include_path, suffix = match.groups()
        new_path = convert_include_path(include_path)
        if new_path != include_path:
            changes.append((include_path, new_path))
        return f"{prefix}{new_path}{suffix}"

    new_text = INCLUDE_RE.sub(replacer, text)
    return (len(changes) > 0, new_text, changes)


def main():
    parser = argparse.ArgumentParser(
        description="Rewrite #include \"...\" filenames from camelCase/PascalCase to snake_case."
    )
    parser.add_argument("root_dir", type=Path, help="Directory to scan recursively")
    parser.add_argument(
        "--extensions", default="hpp,cpp,h",
        help="Comma-separated list of extensions to scan for #include lines (default: hpp,cpp,h)"
    )
    parser.add_argument("--apply", action="store_true", help="Actually rewrite files (default is dry run)")
    args = parser.parse_args()

    root = args.root_dir.resolve()
    if not root.is_dir():
        print(f"Error: {root} is not a directory", file=sys.stderr)
        sys.exit(1)

    extensions = {e.strip().lstrip(".") for e in args.extensions.split(",") if e.strip()}
    files = find_target_files(root, extensions)

    total_files_changed = 0
    total_includes_changed = 0

    for f in files:
        changed, new_text, changes = process_file(f)
        if not changed:
            continue

        total_files_changed += 1
        total_includes_changed += len(changes)

        rel = f.relative_to(root)
        print(f"{rel}:")
        for old, new in changes:
            print(f'  #include "{old}"  ->  #include "{new}"')

        if args.apply:
            f.write_text(new_text, encoding="utf-8")

    print()
    if total_files_changed == 0:
        print("No #include lines needed changes.")
        return

    if args.apply:
        print(f"Done. Rewrote {total_includes_changed} include(s) across {total_files_changed} file(s).")
    else:
        print(
            f"Dry run only — would rewrite {total_includes_changed} include(s) "
            f"across {total_files_changed} file(s). Re-run with --apply to write changes."
        )
        print("(Files are edited in place, not via `git mv` — run `git diff` after --apply to review before committing.)")


if __name__ == "__main__":
    main()
