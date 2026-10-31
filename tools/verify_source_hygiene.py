"""Reject blocked third-party identifiers from tracked source and commit messages."""
from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]

# Byte signatures intentionally avoid embedding the blocked identifiers in the
# repository that this check protects.
SIGNATURES = (
    bytes((105, 110, 115, 105, 103, 104, 116)),
    bytes((115, 107, 105, 110, 45, 99, 111, 114, 101)),
    bytes((99, 115, 50, 45, 105, 110, 115, 105, 103, 104, 116, 45, 97, 103, 101, 110, 116)),
    bytes((112, 111, 108, 121, 102, 111, 114, 109, 32, 110, 111, 110, 99, 111, 109, 109, 101, 114, 99, 105, 97, 108)),
)


def git_bytes(*args: str) -> bytes:
    return subprocess.check_output(("git", *args), cwd=ROOT)


def blocked_signature(data: bytes) -> int | None:
    folded = data.lower()
    compact = folded.replace(b"\0", b"")
    for index, signature in enumerate(SIGNATURES, start=1):
        if signature in folded or signature in compact:
            return index
    return None


def tracked_paths() -> list[Path]:
    raw = git_bytes("ls-files", "-z")
    return [ROOT / item.decode("utf-8", errors="surrogateescape") for item in raw.split(b"\0") if item]


def verify_paths(paths: list[Path]) -> list[str]:
    failures: list[str] = []
    for path in paths:
        relative = path.relative_to(ROOT).as_posix()
        signature = blocked_signature(relative.encode("utf-8", errors="surrogateescape"))
        if signature is not None:
            failures.append(f"path signature {signature}: {relative}")
            continue
        if not path.is_file():
            continue
        signature = blocked_signature(path.read_bytes())
        if signature is not None:
            failures.append(f"content signature {signature}: {relative}")
    return failures


def verify_commit_messages(revision_range: str | None) -> list[str]:
    args = ["log", "--format=%H%x00%B%x00"]
    if revision_range:
        args.append(revision_range)
    else:
        args.extend(("-1", "HEAD"))
    records = git_bytes(*args).split(b"\0")
    failures: list[str] = []
    for index in range(0, len(records) - 1, 2):
        commit = records[index].decode("ascii", errors="replace").strip()
        signature = blocked_signature(records[index + 1])
        if signature is not None:
            failures.append(f"commit-message signature {signature}: {commit}")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--commit-range", help="Git revision range whose messages must be checked")
    args = parser.parse_args()

    failures = verify_paths(tracked_paths())
    failures.extend(verify_commit_messages(args.commit_range))
    if failures:
        print("Source hygiene verification failed:", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1
    print("PASS: tracked paths, contents and commit messages contain no blocked signatures")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
