#!/usr/bin/env python3
"""Generate a machine-readable index of the shipped standard library.

WHY THIS EXISTS
---------------
Every `lib/std/*.fin` file documents itself: each public declaration is preceded by a `//`
block that says what it does, what it does not do, and why. That is the best documentation
in the project and, until now, no tool could read it. An editor that wants to show a hover
for `strings::len` had to either parse Fin with a full parser or keep a hand-written list
that goes stale the first time a module changes.

This turns the real sources into one JSON file, so a language server, a doc site generator,
and the site's search index can all read the same truth. It is deliberately naive about
syntax -- line-oriented, no parser -- because the declaration syntax in this codebase is
regular and a false positive would be visible in review. What it will not do is invent
anything: a declaration it cannot read is skipped and counted, so a change in style shows up
as a number dropping rather than as a silently missing symbol.

Output: docs/stdlib-index.json

    python3 tests/tools/gen_stdlib_index.py            # write
    python3 tests/tools/gen_stdlib_index.py --check    # fail if stale
"""

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STD = ROOT / "lib" / "std"
OUT = ROOT / "docs" / "stdlib-index.json"

# `pub fun name(args) <ret> {`, `pub fun? name(...) <ret>?`, `pub struct Name {`,
# `pub const NAME <type> = value;`, and the `#[export]` attribute line above them.
FUN_RE = re.compile(r"^pub fun(\?)?\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(")
STRUCT_RE = re.compile(r"^pub struct\s+([A-Za-z_][A-Za-z0-9_]*)")
CONST_RE = re.compile(r"^pub const\s+([A-Za-z_][A-Za-z0-9_]*)")
TYPE_RE = re.compile(r"^pub (struct|prototype|enum)\s+([A-Za-z_][A-Za-z0-9_]*)")
NAMESPACE_RE = re.compile(r"^namespace\s+([A-Za-z_][A-Za-z0-9_]*)")
COMMENT_RE = re.compile(r"^//\s?(.*)$")


def module_summary(lines: list[str]) -> str:
    """The module's own summary: the first prose paragraph after the license banner.

    The header is two blocks: the shared project/licence banner, then the module's own
    description. Starting after the banner's closing `// ====` line is what separates them;
    without that, every module's summary in the index would be the GPL text.
    """
    # The header opens and closes with a `// ===` rule; the module's own description
    # follows the *closing* one, so the last rule in the first 40 lines is the boundary.
    start = 0
    for i, line in enumerate(lines[:40]):
        if line.startswith("//") and set(line.strip(" /")) == {"="} and len(line.strip(" /")) > 10:
            start = i + 1

    paragraph = []
    for line in lines[start : start + 80]:
        m = COMMENT_RE.match(line)
        if not m:
            if paragraph:
                break
            continue
        text = m.group(1).strip()
        if text.startswith("=") or text.startswith("-" * 10):
            continue
        if not text:
            if paragraph:
                break
            continue
        paragraph.append(text)
    return " ".join(paragraph)[:400]


def doc_block(lines: list[str], decl_index: int) -> str:
    """The contiguous `//` comment directly above a declaration, and nothing else.

    Walking upward and stopping at the first non-comment line is what keeps a declaration
    from inheriting the previous function's documentation.
    """
    collected = []
    i = decl_index - 1
    # Attributes and blank lines may sit between the doc block and the declaration.
    while i >= 0 and (lines[i].startswith("#[") or lines[i].strip() == ""):
        i -= 1
    while i >= 0:
        m = COMMENT_RE.match(lines[i])
        if not m:
            break
        collected.append(m.group(1).rstrip())
        i -= 1
    collected.reverse()
    text = "\n".join(collected).strip()
    # The header banner repeats in every file; do not attach it to the first symbol.
    if "Fin Programming Language" in text or text.startswith("="):
        return ""
    return text


def signature(lines: list[str], start: int) -> str:
    """The declaration line, joined across a wrap, without the opening brace.

    Declarations in this codebase wrap their parameter list across lines, so one line is
    not enough for a hover. Joining until the brace or the terminator keeps it readable.
    """
    parts = []
    depth = 0
    for i in range(start, min(start + 12, len(lines))):
        line = lines[i].strip()
        parts.append(line)
        depth += line.count("(") - line.count(")")
        depth += line.count("<") - line.count(">")
        if depth <= 0:
            break
    text = " ".join(parts)
    for terminator in (" {", ";"):
        idx = text.find(terminator)
        if idx >= 0:
            text = text[:idx]
    return text.strip()


def parse_module(path: Path) -> dict:
    text = path.read_text(encoding="utf-8")
    lines = text.splitlines()
    members = []
    skipped = 0

    for i, line in enumerate(lines):
        if not line.startswith("pub "):
            continue
        if line.startswith("pub:"):
            continue  # a visibility label inside a struct, not a declaration

        name = None
        kind = None
        nullable = False
        m = FUN_RE.match(line)
        if m:
            nullable = m.group(1) == "?"
            name = m.group(2)
            kind = "fun"
        else:
            m = TYPE_RE.match(line)
            if m:
                kind = m.group(1)
                name = m.group(2)
            else:
                m = CONST_RE.match(line)
                if m:
                    kind = "const"
                    name = m.group(1)

        if not name:
            skipped += 1
            continue

        members.append(
            {
                "kind": kind,
                "name": name,
                "nullable": nullable,
                "signature": signature(lines, i),
                "doc": doc_block(lines, i),
                "line": i + 1,
            }
        )

    return {
        "module": path.stem,
        "path": str(path.relative_to(ROOT)).replace("\\", "/"),
        "summary": module_summary(lines),
        "members": members,
        "skipped_public_lines": skipped,
        # Stated rather than hidden: a hover for a member with no doc block has nothing to
        # show, and a consumer should present the signature alone rather than an empty
        # paragraph. `docs/known-issues.md` records the count as a documentation gap.
        "members_without_doc": sum(1 for m in members if not m["doc"]),
    }


def build() -> dict:
    modules = []
    for path in sorted(STD.glob("*.fin")):
        modules.append(parse_module(path))
    return {
        "schema": 1,
        "generated_from": "lib/std/*.fin",
        "note": (
            "Generated by tests/tools/gen_stdlib_index.py. Do not edit by hand: the module "
            "sources are authoritative and this file is a projection of them. Regenerate "
            "and commit when a module's public surface changes."
        ),
        "module_count": len(modules),
        "member_count": sum(len(m["members"]) for m in modules),
        "member_count_without_doc": sum(m["members_without_doc"] for m in modules),
        "modules": modules,
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="report staleness, write nothing")
    args = ap.parse_args()

    data = build()
    rendered = json.dumps(data, indent=2, ensure_ascii=False) + "\n"

    if args.check:
        if not OUT.exists():
            print(f"error: {OUT.relative_to(ROOT)} does not exist; run without --check")
            return 1
        if OUT.read_text(encoding="utf-8") != rendered:
            print(
                f"error: {OUT.relative_to(ROOT)} is stale. "
                f"Run python3 tests/tools/gen_stdlib_index.py and commit the result."
            )
            return 1
        print(
            f"stdlib index: current ({data['module_count']} modules, "
            f"{data['member_count']} members)"
        )
        return 0

    OUT.write_text(rendered, encoding="utf-8")
    print(
        f"wrote {OUT.relative_to(ROOT)}: {data['module_count']} modules, "
        f"{data['member_count']} members"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
