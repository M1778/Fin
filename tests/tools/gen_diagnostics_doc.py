#!/usr/bin/env python3
"""Regenerate docs/diagnostics.md from `finc --explain`.

The registry in src/diagnostics/DiagnosticEngine.cpp is the only place a code is
defined; this script formats it into the page and never invents a code. Run it
after any registry change instead of editing the page by hand:

    uv run --no-project tests/tools/gen_diagnostics_doc.py build-explain/finc

`tests/test_diagnostic_help.cpp` then checks that every inventory line appears in
the page and that each example the page quotes is the one `--explain` prints.
"""
import argparse
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

GROUPS = [
    ("E01", "Backend",
     "Code generation. The front end accepted the program and lowering could not proceed."),
    ("E02", "Types", "A value's type does not fit the position the program puts it in."),
    ("E03", "Names", "A name, member, or method that does not resolve where it was written."),
    ("E04", "Declarations",
     "A declaration that cannot stand as written: generic arity, conformance, layout, attributes."),
    ("E05", "Operations",
     "An operator, statement, or pattern the operand types cannot support."),
    ("E06", "Modules", "The loader could not find, load, or satisfy an import."),
    ("E07", "Ownership", "A reference that would outlive the storage it names."),
    ("E08", "Invocation",
     "Reserved: a diagnostic about the command line rather than a place in a file."),
    ("E09", "Lexer and syntax", "Reserved: source that is not a program."),
]

PREAMBLE = """# Diagnostic codes

`finc` names its diagnostics with stable codes. A code identifies a *rule*, not one
message: `E0201` is every type mismatch, whatever the two types are, and the message on
the diagnostic names the specifics.

```
$ finc --explain E0201
$ finc --explain list
```

`--explain <code>` prints the long form below on stdout; `--explain list` prints the
inventory, sorted by code. Neither needs a source file. An unknown code is a
command-line error (exit `2`) naming it — a code this compiler does not raise is usually
a version skew between `finc` and whatever printed the code. See
[the interface contract](finc-interface-contract.md#explaining-diagnostics).

This page is generated from the same table the compiler assigns codes from, so it cannot
list a code nothing raises or omit one that something does:
`tests/test_diagnostic_help.cpp` checks the inventory against this file and compiles every
example below to prove it still reproduces its own code.

## How the numbers are laid out

The hundreds digit names the pass that owns the rule, so a code says where to look before
you read its message. `E0101` and `E0201` were published before the scheme and keep their
numbers; everything else was appended around them.

| Range | Family | What it covers |
| --- | --- | --- |
"""

CLOSING = """A diagnostic that matches no row still renders `"code":null` in
`--diagnostics=json`. That is not an omission in this page: a code is added with the rule
that raises it, and a code is never invented to fill a table.

A family is left uncoded when its message shape is too vague to key on without stealing
another family's messages — the syntax family (`syntax error, ...`, `expected ';'`) and
the lexer's catch-all are the current examples: their text is shared with parser internals
that are not one rule. A row that mis-matches is worse than no row, so those stay `null`
until a predicate exists that is both specific and complete.

## Adding a code

A new code is a new row in `kCodeRegistry`
(`src/diagnostics/DiagnosticEngine.cpp`), which is both the matcher that stamps a code onto
an emitted message and the table `--explain` answers from. One row carries the code, the
message shape it matches, its severity, this page's wording, an example, and the ruling
that owns it; so a code cannot be raised without an explanation, and an explanation cannot
describe a code nothing raises. Regenerate this page from that table:

```sh
uv run --no-project tests/tools/gen_diagnostics_doc.py build/finc
```

A row is only added when its `prefix`/`contains` pair matches every call site in the
family and nothing outside it. That is enforced, not asserted:
`DiagnosticHelp.NoTwoCodesMatchTheSameMessage` and
`DiagnosticHelp.EveryRegisteredPrefixIsUniqueOrDisjoint` in
`tests/test_diagnostic_help.cpp` apply the real match rules from `diagnosticRules()`.
"""


def explain(finc: str, code: str | None = None) -> str:
    argv = [finc, "--explain"] + ([code] if code else ["list"])
    result = subprocess.run(argv, capture_output=True, text=True, timeout=60)
    if result.returncode != 0:
        raise SystemExit(f"{argv} exited {result.returncode}\n{result.stderr}")
    return result.stdout


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("finc", help="the compiler to read the catalogue from")
    args = parser.parse_args()

    rows = []
    for line in explain(args.finc).splitlines():
        if line.strip():
            code, severity, summary = line.split(None, 2)
            rows.append((code, severity, summary))

    out = [PREAMBLE]
    present = {code[:3] for code, _, _ in rows}
    for prefix, name, blurb in GROUPS:
        state = "" if prefix in present else " *(reserved, no codes yet)*"
        out.append(f"| `{prefix}xx` | {name} | {blurb}{state} |\n")

    out.append("\n## The inventory\n\n| Code | Severity | Meaning |\n| --- | --- | --- |\n")
    for code, severity, summary in rows:
        out.append(f"| `{code}` | {severity} | {summary[0].upper()}{summary[1:]} |\n")

    out.append("\n")
    for code, severity, summary in rows:
        text = explain(args.finc, code)
        head, body = text.split("\n", 1)
        assert head.startswith(code), head
        body = body.lstrip("\n")
        # The example is quoted verbatim by the catalogue test, so it is inserted
        # unchanged; only the fence is added, classified so the docs gate can
        # compile it. E0101 needs `-c` to reach lowering, everything else fails
        # in the front end.
        marker = "Example:\n\n"
        at = body.find(marker)
        if at >= 0:
            end = body.find("\n\n", at + len(marker))
            example = body[at + len(marker):end if end >= 0 else len(body)]
            mode = "build-error" if code == "E0101" else "error"
            tail = body[end:].lstrip("\n") if end >= 0 else ""
            body = body[:at] + marker + f"```fin {mode}\n{example}\n```\n\n" + tail
        out.append(f"## `{code}` — {summary}\n\n{body}")
        if not body.endswith("\n"):
            out.append("\n")
        out.append("\n")

    out.append(CLOSING)
    (ROOT / "docs" / "diagnostics.md").write_text("".join(out), encoding="utf-8")
    print(f"wrote docs/diagnostics.md: {len(rows)} codes")


if __name__ == "__main__":
    main()
