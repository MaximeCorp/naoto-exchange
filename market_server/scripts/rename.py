#!/usr/bin/env python3
"""
rename_to_snake_case.py

Recursively renames .hpp (and optionally .h/.cpp) files with
camelCase or PascalCase names to snake_case.

Examples:
    EventHandlerBase.hpp   -> event_handler_base.hpp
    orderRequest.hpp       -> order_request.hpp
    FlatHashMap.hpp        -> flat_hash_map.hpp
    already_snake.hpp      -> unchanged

Usage:
    python3 rename_to_snake_case.py <root_dir> [--extensions hpp,h,cpp] [--apply] [--git]

By default this does a DRY RUN and only prints what it would rename.
Pass --apply to actually perform the renames.
Pass --git to use `git mv` instead of a plain filesystem rename
(preserves file history — recommended if root_dir is inside a git repo).
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path


def to_snake_case(stem: str) -> str:
    """
    Convert a camelCase or PascalCase identifier to snake_case.
    Leaves already-snake_case or already-lowercase names unchanged.
    Handles acronym runs sensibly, e.g. 'FDGenID' -> 'fd_gen_id'.
    """
    # Insert an underscore between a lowercase/digit and an uppercase letter
    # e.g. "orderRequest" -> "order_Request"
    s1 = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", stem)
    # Insert an underscore between an acronym run and a following
    # Capitalized word, e.g. "FDGenID" -> "FD_Gen_ID" -> handled progressively
    s2 = re.sub(r"(?<=[A-Z])(?=[A-Z][a-z])", "_", s1)
    return s2.lower()


def find_target_files(root: Path, extensions: set[str]) -> list[Path]:
    files = []
    for ext in extensions:
        files.extend(root.rglob(f"*.{ext}"))
    return sorted(set(files))


def plan_renames(files: list[Path]) -> list[tuple[Path, Path]]:
    plan = []
    for f in files:
        new_stem = to_snake_case(f.stem)
        if new_stem == f.stem:
            continue  # already snake_case, nothing to do
        new_path = f.with_name(f"{new_stem}{f.suffix}")
        if new_path.exists() and new_path != f:
            print(f"WARNING: skipping {f} -> {new_path} (target already exists)", file=sys.stderr)
            continue
        plan.append((f, new_path))
    return plan


def apply_renames(plan: list[tuple[Path, Path]], use_git: bool) -> None:
    for old, new in plan:
        if use_git:
            result = subprocess.run(
                ["git", "mv", str(old), str(new)],
                capture_output=True, text=True
            )
            if result.returncode != 0:
                print(f"ERROR: git mv failed for {old} -> {new}: {result.stderr.strip()}", file=sys.stderr)
                continue
        else:
            old.rename(new)
        print(f"renamed: {old} -> {new}")


def main():
    parser = argparse.ArgumentParser(description="Rename camelCase/PascalCase files to snake_case.")
    parser.add_argument("root_dir", type=Path, help="Directory to scan recursively")
    parser.add_argument(
        "--extensions", default="hpp",
        help="Comma-separated list of extensions to target, without dots (default: hpp)"
    )
    parser.add_argument("--apply", action="store_true", help="Actually perform renames (default is dry run)")
    parser.add_argument("--git", action="store_true", help="Use `git mv` instead of a plain rename (preserves history)")
    args = parser.parse_args()

    root = args.root_dir.resolve()
    if not root.is_dir():
        print(f"Error: {root} is not a directory", file=sys.stderr)
        sys.exit(1)

    extensions = {e.strip().lstrip(".") for e in args.extensions.split(",") if e.strip()}

    files = find_target_files(root, extensions)
    plan = plan_renames(files)

    if not plan:
        print("Nothing to rename — all matching files are already snake_case.")
        return

    print(f"{'Would rename' if not args.apply else 'Renaming'} {len(plan)} file(s):\n")
    for old, new in plan:
        print(f"  {old.relative_to(root)}  ->  {new.relative_to(root)}")

    if not args.apply:
        print("\nDry run only — no files were changed. Re-run with --apply to perform renames.")
        if args.git:
            print("(--git will be used to preserve file history)")
        return

    print()
    apply_renames(plan, use_git=args.git)
    print(f"\nDone. {len(plan)} file(s) renamed.")
    if not args.git:
        print("Note: plain rename was used — if this is a git repo, run `git add -A` to stage the moves,")
        print("or re-run with --git next time to use `git mv` and preserve file history automatically.")


if __name__ == "__main__":
    main()
