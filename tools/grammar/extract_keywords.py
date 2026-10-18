#!/usr/bin/env python3
"""Extract the Fin keyword lists from src/lexer/lexer.l into tools/grammar/fin-keywords.json.

Single source of truth for the highlighters (VS Code tmLanguage, Pygments, Chroma,
highlight.js) and tree-sitter keyword lists: they consume this JSON and must never
hand-copy from `lexer.l`. The JSON is committed; regenerate with:

    python3 tools/grammar/extract_keywords.py

Only keyword rules are extracted: `"word" { MAKE(KW_* | TYPE_* | HASH_*); }` lines
in the rule table (between the two `%%` markers). Operators, punctuation and the
literal rules (`MAKE_VAL`) are out of scope on purpose -- every highlighter writes
its own operator patterns, and a keyword drift in prose comments (the keyword
`match` was lost in prose before) must not reach the JSON.
"""

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LEXER = ROOT / "src/lexer/lexer.l"
OUT = ROOT / "tools/grammar/fin-keywords.json"

# One lexer rule line: quoted word, then an action whose token name is MADE.
RULE = re.compile(r'^"([^"]+)"\s*\{\s*MAKE\s*\(\s*([A-Za-z0-9_]+)\s*\);?.*?\}\s*$', re.M)

SECTION_OF_KIND = {"KW_": "keywords", "TYPE_": "types", "HASH_": "directives"}


def extract() -> dict:
    rules = LEXER.read_text(encoding="utf-8").split("%%")[1]
    result = {name: [] for name in SECTION_OF_KIND.values()}
    for match in RULE.finditer(rules):
        word, kind = match[1], match[2]
        # Comment rules ("//".* etc.) cannot match RULE: their actions are not a
        # bare MAKE(KIND); ... }. Rule actions need no MAKE_VAL; its two quotes
        # make the word pattern unmatchable.
        prefix = next((p for p in SECTION_OF_KIND if kind.startswith(p)), None)
        if prefix:
            result[SECTION_OF_KIND[prefix]].append(word)
    return result


def main() -> int:
    data = extract()

    # Self-checks: fail loudly on drift or extraction bugs rather than emit junk.
    words = [word for section in data.values() for word in section]
    dupes = {word for word in words if words.count(word) > 1}
    assert not dupes, f"duplicate keyword rule: {sorted(dupes)}"
    for must in ("blame", "m1778", "noret", "match"):
        present = any(must in section for section in data.values())
        assert present, f"{must!r} missing from extraction"
    for word in words:
        assert word.isprintable() and len(word) > 1, f"questionable rule word {word!r}"

    out = {
        "_no_hand_edit": "generated from src/lexer/lexer.l by tools/grammar/extract_keywords.py",
        **{name: sorted(section) for name, section in data.items()},
    }
    OUT.write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8")
    counts = ", ".join(f"{len(v)} {k}" for k, v in out.items() if not k.startswith("_"))
    print(f"wrote {OUT.relative_to(ROOT)} ({counts})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
