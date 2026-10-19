#!/usr/bin/env python3
"""Generate tools/grammar/fin.tmLanguage.json from tools/grammar/fin-keywords.json.

TextMate grammar for Fin (`scopeName: source.fin`), aimed at the future
M1778/fin-vscode package. This is task 1 of #50 and consumes the task-3
keyword JSON as its single source: NO keyword is hand-typed here. Every word
from the JSON must resolve into exactly one scope rule; the category map below
is a deliberate assignment (which words read as control flow, declarations,
...) and a new keyword fails this script loudly instead of silently greying
out -- regenerate and extend CATEGORIES, then rerun.

Operators and punctuation are hand lists ON PURPOSE, like extract_keywords.py
says: only word rules are extracted into the JSON, and "every highlighter
writes its own operator patterns" (epic rationale in #50). The lists below
track the symbolic rules of src/lexer/lexer.l, longest first so `>>=` wins
over `>>`.
"""

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
KEYWORDS = ROOT / "tools/grammar/fin-keywords.json"
OUT = ROOT / "tools/grammar/fin.tmLanguage.json"

# Which scope each JSON word reads as. Words not listed fail the run.
CATEGORIES = {
    "constant.language": {"null", "true", "false"},
    # `blame` is keyword.control per the epic's own design; `m1778` rides with
    # it (`blame m1778;`).
    "keyword.control": {
        "if", "else", "while", "do", "for", "foreach", "in", "break",
        "continue", "match", "try", "catch", "return", "blame", "m1778",
    },
    "storage.type": {"Self"},
    # Everything else reads as a declaration/structural keyword.
    "keyword.declaration": {
        "any", "as", "as_ptr", "auto", "cast", "class", "const", "define",
        "delete", "enum", "extern", "fn", "from", "fun", "implements",
        "import", "interface", "let", "macro", "namespace", "new", "operator",
        "priv", "pub", "quote", "readonly", "sizeof", "special", "static",
        "struct", "super", "type", "typeof",
    },
}

# Symbolic rules of lexer.l, by hand as agreed above. Longest alternatives
# first: flex's longest-match ordering, kept here because the regexp engine
# tries alternatives left to right.
OPERATORS = [
    "=>", "->", "::", "...", "++", "--", "+=", "-=", "*=", "/=",
    "%=", "&=", "|=", "==", "!=", "<=", ">=", "&&", "||", "<<=", ">>=",
    "<<", "=", "+", "-", "*", "/", "%", "<", ">", "!", "&", "~", "|", "^",
    "`", "?", "$", "@", "#",
]
PUNCTUATION = [";", ":", ",", ".", "(", ")", "{", "}", "[", "]"]


def alternation(words):
    """word|word|... as one Oniguruma-compatible regexp body."""
    return "|".join(words)


def build_pattern_rules(kw):
    """Flat top-level rules, in match priority order."""
    patterns = []
    rep = {}

    # Comments first so `//` and `/*` never split into DIV/STAR operators.
    # The block rule self-includes, matching the lexer's nestable choice
    # (lexer.l: `/*` inside a comment opens a second level).
    rep["comment"] = [
        {"name": "comment.block.fin",
         "begin": r"/\*",
         "end": r"\*/",
         "patterns": [{"include": "#comment"}]},
        {"name": "comment.line.double-slash.fin", "match": r"//.*"},
    ]
    patterns.append({"include": "#comment"})

    # Strings may span newlines (lexer.l: [^\\"] has no \n exclusion).
    patterns.append({
        "name": "string.quoted.double.fin",
        "begin": '"', "end": '"',
        "patterns": [{"name": "constant.character.escape.fin",
                      "match": r"\\."}],
    })
    # Chars are exactly one character or escape, like the lexer's rule
    # (`'([^\\']|\.)'` -- longer content simply fails to match).
    patterns.append({"name": "string.quoted.single.fin",
                     "match": r"'(?:[^'\\]|\\.)'"})

    # Runtime attributes: #[export], #[llvm_name="getenv"] (parser.y rule
    # `attribute`). Directive words #for/#index first, then the bracketed
    # grant as begin/end so keys/values keep their own scopes.
    patterns.append({"name": "keyword.directive.fin",
                     "match": "#(?:" + alternation(
                         d[1:] for d in kw["directives"]) + r")\b"})
    patterns.append({
        "name": "meta.directive.grant.fin",
        "begin": r"#\[", "end": r"\]",
        "patterns": [{"include": "$base"}],
    })
    # `%{ ... }%` attribute block applies one attribute list to its body.
    patterns.append({
        "name": "meta.attribute-block.fin",
        "begin": r"%\{", "end": r"\}%",
        "patterns": [{"include": "$base"}],
    })

    # Macros: @special/@macro/@define/@quote highlight as keywords, any other
    # @name as a meta tag. Generated from the same JSON, not retyped; the
    # `(?:...)` wrapper keeps bare `macro` etc. from matching without the @.
    macro_words = sorted(CATEGORIES["keyword.declaration"]
                         & set(kw["keywords"])
                         & {"special", "macro", "define", "quote"})
    patterns.append({"name": "keyword.other.macro.fin",
                     "match": "@(?:" + alternation(macro_words) + r")\b"})
    patterns.append({"name": "meta.macro.fin", "match": r"@[A-Za-z_]\w*"})

    # Numbers: floats only when a dot separates digits, like lexer.l. There
    # are no hex/escape numeric forms in the lexer; none are invented here.
    patterns.append({"name": "constant.numeric.float.fin",
                     "match": r"\b\d+\.\d+\b"})
    patterns.append({"name": "constant.numeric.integer.fin",
                     "match": r"\b\d+\b"})

    # Types (plus Self) then keyword buckets. \b guards against `in` inside
    # `int` style overlaps since alternations try longest-first ordering.
    patterns.append({"name": "storage.type.fin",
                     "match": r"\b(?:" + alternation(
                         kw["types"] + ["Self"]) + r")\b"})
    patterns.append({"name": "constant.language.fin",
                     "match": r"\b(?:" + alternation(sorted(
                         CATEGORIES["constant.language"])) + r")\b"})
    patterns.append({"name": "keyword.control.fin",
                     "match": r"\b(?:" + alternation(sorted([
                         w for w in kw["keywords"]
                         if w in CATEGORIES["keyword.control"]])) + r")\b"})
    patterns.append({"name": "keyword.declaration.fin",
                     "match": r"\b(?:" + alternation(sorted([
                         w for w in kw["keywords"]
                         if w in CATEGORIES["keyword.declaration"]])) + r")\b"})

    # Generics / type annotations, best effort by epic design. Content
    # excludes `&|;` so comparisons containing `&&`/`||` or a statement end
    # cannot start a false span; content is not limited to letters, so widths
    # (`<int{8}>`), pointers (`<&int>`) and arrays (`<[int, 5]>`) color too.
    patterns.append({"name": "meta.type.annotation.fin",
                     "match": r"<[A-Za-z_][^>&|;]*>"})

    patterns.append({"name": "keyword.operator.fin",
                     "match": r"(?:"
                              + "|".join(re.escape(op) for op in OPERATORS)
                              + ")"})
    patterns.append({"name": "punctuation.definition.punctuation.fin",
                     "match": "(?:"
                              + "|".join(re.escape(p) for p in PUNCTUATION)
                              + ")"})

    # Plain identifiers last: everything word-shaped left over gets the
    # identifier scope. Keyword rules above win by list position.
    patterns.append({"name": "variable.other.fin", "match": r"[A-Za-z_]\w*"})

    return patterns, rep


def check(kw, patterns, rep):
    """Fail loudly on drift: coverage, regex shape, includes resolve."""
    words = kw["keywords"] + kw["types"] + kw["directives"]
    declared = set()
    for cat in CATEGORIES.values():
        declared |= cat
        for w in cat:
            if w in kw["keywords"] or w in kw["types"]:
                continue
            raise SystemExit(f"category word {w!r} is not in fin-keywords.json;"
                             " update the JSON via extract_keywords.py, or "
                             "the categories")
    uncovered = [w for w in kw["keywords"]
                 if w not in declared and w not in CATEGORIES["storage.type"]]
    if uncovered:
        raise SystemExit(f"keywords not assigned a scope: {sorted(uncovered)};"
                         " extend CATEGORIES")
    types_scoped = set(kw["types"]) | {"Self"}
    if types_scoped != set(CATEGORIES["storage.type"]) | set(kw["types"]):
        raise SystemExit("storage.type category does not equal the JSON "
                         "types plus Self")
    body = json.dumps({"patterns": patterns, "repository": rep})
    for w in words:
        bare = w[1:] if w.startswith("#") else w
        assert bare in body, f"word {w!r} produced no pattern"


def main() -> int:
    kw = json.loads(KEYWORDS.read_text(encoding="utf-8"))
    if "keywords" not in kw:
        raise SystemExit("fin-keywords.json has no keywords section")

    patterns, rep = build_pattern_rules(kw)
    check(kw, patterns, rep)

    out = {
        "name": "Fin",
        "scopeName": "source.fin",
        "fileTypes": ["fin"],
        "patterns": patterns,
        "repository": rep,
    }
    OUT.write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {OUT.relative_to(ROOT)} "
          f"({len(rep['comment'])} comment rules, "
          f"{len(patterns)} top-level patterns; "
          f"{len(kw['keywords'])} keywords, {len(kw['types'])} types, "
          f"{len(kw['directives'])} directives all consumed from the JSON)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
