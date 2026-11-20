#!/usr/bin/env python3
"""Derive editors/fin-keywords.json and the three grammar artifacts from the compiler.

Sources of truth, all read here and never retyped in the outputs:
  src/lexer/lexer.l                  keyword rules and the symbolic rule table
  src/semantics/impl/Analyzer_Core.cpp  primitive type names registered as builtins

The checker also cross-checks the JSON word list and token kinds against
lib/std/token.fin and fails on any delta beyond the two documented in README.md.

Run:  python3 editors/scripts/generate.py
Then: python3 editors/scripts/check_editors.py   (re-derives and fails on drift)
"""

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LEXER = ROOT / "src/lexer/lexer.l"
ANALYZER = ROOT / "src/semantics/impl/Analyzer_Core.cpp"
OUT = ROOT / "editors"
KEYWORDS = OUT / "fin-keywords.json"

# scope -> which half of the language the word introduces. Every lexer word must
# appear here exactly once; a new lexer keyword fails this script instead of
# silently losing its colour.
CATEGORIES = {
    "keyword.control": {
        "if", "else", "while", "do", "for", "foreach", "match", "break",
        "continue", "return", "try", "catch", "blame",
    },
    "keyword.declaration": {
        "let", "const", "fun", "struct", "enum", "interface", "namespace",
        "macro", "static", "operator", "type", "class", "implements",
        "special", "define", "extern", "import", "from", "delete",
    },
    "keyword.modifier": {"pub", "priv", "readonly"},
    "keyword.operator.word": {"new", "sizeof", "typeof", "as_ptr", "cast", "in"},
    "keyword.other": {"as", "quote"},
    "storage.type": {"any", "Self", "auto", "fn"},
    "constant.language": {"null", "true", "false", "m1778"},
    "variable.language": {"super"},
}

# Which of those are expression-level rather than statement-introducing.
EXPRESSION = {
    "in", "new", "sizeof", "typeof", "as_ptr", "cast",
    "null", "true", "false", "m1778", "super", "quote", "as",
}
MODIFIERS = CATEGORIES["keyword.modifier"]

# Primitive spellings the lexer does not reserve but the compiler registers as
# builtin types (Analyzer_Core.cpp). Kept separate from `types` so the JSON says
# which list came from the lexer and which from the semantic layer.
BUILTIN_TYPES = [
    "short", "ushort", "uint", "ulong", "object",
    "u8", "i8", "u16", "i16", "u32", "i32", "u64", "i64", "f32", "f64",
    "usize", "isize", "size_t",
]

RULE = re.compile(r'^"([^"]+)"\s*\{\s*MAKE(?:_VAL)?\(([A-Za-z0-9_]+)\)', re.M)


def read_lexer():
    """Return [(word, kind)] for every quoted rule in the lexer's rule table."""
    body = LEXER.read_text(encoding="utf-8").split("%%")[1]
    return [(w, k) for w, k in RULE.findall(body)]


def is_word(word):
    return word[0].isascii() and word[0].isalpha()


def build_keywords(rules):
    kinds = dict(rules)
    # TYPE_/HASH_ rules have their own JSON sections; CATEGORIES covers the
    # remaining KW_ words only.
    words = [w for w, k in rules if is_word(w) and k.startswith("KW_")]
    assigned = {}
    for scope, members in CATEGORIES.items():
        for word in members:
            if word in assigned:
                raise SystemExit(f"{word!r} in two categories")
            if word not in words:
                raise SystemExit(
                    f"category word {word!r} has no lexer rule; update CATEGORIES")
            assigned[word] = scope
    missing = sorted(set(words) - set(assigned))
    if missing:
        raise SystemExit(
            f"lexer keywords with no scope: {missing}; extend CATEGORIES")

    def entry(word):
        scope = assigned.get(word, "storage.type")
        form = ("type" if scope == "storage.type"
                else "modifier" if word in MODIFIERS
                else "expression" if word in EXPRESSION
                else "statement")
        return {"word": word, "scope": scope, "form": form, "token": kinds[word]}

    keywords = [entry(w) for w in sorted(words)]
    type_words = [w for w, k in rules if is_word(w) and k.startswith("TYPE_")]
    dir_words = [w for w, k in rules if k.startswith("HASH_")]
    types = [entry(w) for w in sorted(type_words)]
    directives = [{"word": w, "scope": "keyword.directive", "form": "statement",
                   "token": kinds[w]} for w in sorted(dir_words)]
    return keywords, types, directives


def build_symbols(rules):
    """Operators and punctuation: every non-word lexer rule, longest first."""
    directives = {kind for _, kind in rules if kind.startswith("HASH_")}
    symbols = [w for w, k in rules
               if not is_word(w) and k not in directives]
    punctuation = {"(", ")", "{", "}", "[", "]", ";", ":", ",", ".", "@", "$",
                   "#", "%{", "}%"}
    ops = [s for s in symbols if s not in punctuation]
    ops.sort(key=lambda s: (-len(s), s))
    punct = sorted(punctuation, key=lambda s: (-len(s), s))
    unknown = sorted(set(symbols) - set(ops) - set(punct))
    if unknown:
        raise SystemExit(f"unclassified lexer symbols: {unknown}")
    return ops, punct


def build_builtin_types():
    source = ANALYZER.read_text(encoding="utf-8")
    missing = [t for t in BUILTIN_TYPES if f'defineType("{t}"' not in source]
    if missing:
        raise SystemExit(
            f"builtin types not registered in Analyzer_Core.cpp: {missing}")
    return [{"word": t, "scope": "storage.type", "form": "type"}
            for t in BUILTIN_TYPES]


META = set("\\^$.*+?()[]{}|")


def esc(symbol):
    """Escape exactly the regex metacharacters; keep the pattern readable."""
    return "".join("\\" + c if c in META else c for c in symbol)


def alternation(words):
    return "|".join(esc(w) for w in words)


def tm_language(data):
    """TextMate grammar (source.fin) generated from the keyword JSON."""
    kw = data["keywords"]
    by_scope = {}
    for entry in kw:
        by_scope.setdefault(entry["scope"], []).append(entry["word"])
    types = [e["word"] for e in data["types"]]
    builtin = [e["word"] for e in data["builtin_types"]]
    directives = [e["word"] for e in data["directives"]]

    def words(scope):
        return alternation(sorted(by_scope[scope]))

    # A TextMate repository value is ONE rule object, not an array: the engine
    # silently ignores an array, so a grouped rule here is a `patterns` rule.
    repository = {
        "comment": {"patterns": [
            {"name": "comment.block.fin", "begin": r"/\*", "end": r"\*/",
             # lexer.l: `/*` inside a block comment opens another level.
             "patterns": [{"include": "#comment"}]},
            {"name": "comment.line.double-slash.fin", "match": r"//.*"},
        ]},
        "preprocessor": {
            "name": "keyword.control.preprocessor.fin",
            "match": r"^\s*#(?:c_[a-z]+|cdef)\b.*$",
        },
        "attribute": {
            "name": "meta.attribute.fin", "begin": r"#\[", "end": r"\]",
            "patterns": [{"include": "#string"},
                         {"include": "#constant-numeric"}],
        },
        "attribute-block": {
            "name": "meta.attribute-block.fin", "begin": r"%\{", "end": r"\}%",
            "patterns": [{"include": "$self"}],
        },
        "string": {"patterns": [
            {"name": "string.quoted.double.fin", "begin": r'"', "end": r'"',
             "patterns": [{"name": "constant.character.escape.fin",
                           "match": r"\\."}]},
            {"name": "string.quoted.single.fin",
             "match": r"'(?:[^'\\]|\\.)'"},
        ]},
        "constant-numeric": {"patterns": [
            {"name": "constant.numeric.float.fin", "match": r"\b\d+\.\d+\b"},
            {"name": "constant.numeric.integer.fin", "match": r"\b\d+\b"},
        ]},
        "declaration": {"patterns": [
            {"name": "meta.function.fin", "match": r"\b(fun)(\?)?\s+([A-Za-z_]\w*)",
             "captures": {"1": {"name": "storage.type.function.fin"},
                          "2": {"name": "keyword.operator.nullable.fin"},
                          "3": {"name": "entity.name.function.fin"}}},
            {"name": "meta.type-declaration.fin",
             "match": r"\b(struct|enum|interface|class|type)\s+([A-Za-z_]\w*)",
             "captures": {"1": {"name": "keyword.declaration.fin"},
                          "2": {"name": "entity.name.type.fin"}}},
            {"name": "meta.macro-declaration.fin",
             "match": r"@(macro|special|define)\s+([A-Za-z_]\w*)",
             "captures": {"1": {"name": "keyword.other.macro.fin"},
                          "2": {"name": "entity.name.function.fin"}}},
            {"name": "meta.macro-call.fin",
             "match": r"\b([A-Za-z_]\w*)!",
             "captures": {"1": {"name": "entity.name.function.macro.fin"}}},
        ]},
    }

    patterns = [{"include": "#comment"},
                {"include": "#preprocessor"},
                {"include": "#attribute"},
                {"include": "#attribute-block"},
                {"include": "#string"},
                {"include": "#constant-numeric"},
                {"include": "#declaration"}]
    by_scope.setdefault("storage.type", [])
    by_scope["storage.type"] = sorted(set(by_scope["storage.type"]) | set(types)
                                      | set(builtin))
    for scope in sorted(by_scope):
        patterns.append({"name": scope + ".fin",
                         "match": r"\b(?:" + words(scope) + r")\b"})
    patterns.append({"name": "keyword.directive.fin",
                     "match": r"(?:" + alternation(sorted(directives)) + r")\b"})
    patterns.append({"name": "keyword.operator.fin",
                     "match": "(?:" + alternation(data["operators"]) + ")"})
    patterns.append({"name": "punctuation.fin",
                     "match": "(?:" + alternation(data["punctuation"]) + ")"})
    patterns.append({"name": "variable.other.fin", "match": r"[A-Za-z_]\w*"})

    return {
        "name": "Fin",
        "scopeName": data["scopeName"],
        "fileTypes": data["extensions"],
        "patterns": patterns,
        "repository": repository,
    }


HLJS_GROUP = [
    ("keyword.control", "keyword"),
    ("keyword.declaration", "keyword"),
    ("keyword.modifier", "keyword"),
    ("keyword.other", "keyword"),
    ("keyword.operator.word", "keyword"),
    ("storage.type", "type"),
    ("constant.language", "literal"),
    ("variable.language", "variable"),
]


def hljs_group(scope):
    for prefix, group in HLJS_GROUP:
        if scope.startswith(prefix):
            return group
    raise SystemExit(f"no highlight.js keyword group for scope {scope!r}")


def highlightjs(data):
    groups = {}
    for entry in data["types"] + data["builtin_types"] + data["keywords"]:
        groups.setdefault(hljs_group(entry["scope"]), []).append(entry["word"])
    keyword_lines = []
    for group in sorted(groups):
        keyword_lines.append(f"      {group}: {json.dumps(sorted(groups[group]))},")
    keywords = "\n".join(keyword_lines)
    operators = alternation(data["operators"])
    directives = alternation([e["word"].lstrip("#")
                              for e in data["directives"]])
    tmpl = """// SPDX-License-Identifier: GPL-3.0-or-later
// GENERATED by editors/scripts/generate.py from editors/fin-keywords.json --
// do not edit. Only the stable `hljs` interface is used (v10+).
//
// Limitation: highlight.js has no nested block-comment state, so `/* /* */ */`
// closes at the first `*/`. lexer.l nests. Everything else is one-to-one.

function fin(hljs) {
  return {
    name: 'Fin',
    aliases: ['fin'],
    keywords: {
@@KEYWORDS@@
    },
    contains: [
      hljs.C_LINE_COMMENT_MODE,
      hljs.C_BLOCK_COMMENT_MODE,
      { className: 'meta', begin: /^\\s*#(?:c_[a-z]+|cdef)\\b.*$/ },
      { className: 'meta', begin: /#\\[/, end: /\\]/,
         contains: [hljs.NUMBER_MODE, hljs.QUOTE_STRING_MODE] },
      { className: 'meta', begin: /%\\{/, end: /\\}%/,
         contains: [hljs.NUMBER_MODE] },
      { className: 'meta', begin: /#(?:@@DIRECTIVES@@)\\b/ },
      { className: 'function', begin: /\\bfun\\??\\s+/, end: /[<({;]/,
         excludeEnd: true, relevance: 0,
         contains: [{ className: 'title.function', begin: /[A-Za-z_]\\w*/ }] },
      { className: 'type',
         begin: /\\b(?:struct|enum|interface|class|type)\\s+/,
         end: /[<{;]/,
         excludeEnd: true, relevance: 0,
         contains: [{ className: 'title.class', begin: /[A-Za-z_]\\w*/ }] },
      { className: 'meta', begin: /@(?:macro|special|define)\\s+/,
         end: /[({<]/, excludeEnd: true, relevance: 0,
         contains: [{ className: 'title.function', begin: /[A-Za-z_]\\w*/ }] },
      { className: 'symbol', begin: /\\b[A-Za-z_]\\w*!/ },
      {
        className: 'string',
        begin: /"/, end: /"/,
        contains: [{ className: 'char.escape', begin: /\\\\./ }],
      },
      {
        className: 'string',
        begin: /'/, end: /'/,
        contains: [{ className: 'char.escape', begin: /\\\\./ }],
      },
      { className: 'number', begin: /\\b\\d+\\.\\d+/ },
      { className: 'number', begin: /\\b\\d+/ },
      { className: 'operator', begin: RegExp(@@OPERATORS@@) },
    ],
  };
}

// The definition is the export. Register it with the hljs instance you loaded:
//   const hljs = require('highlight.js');
//   hljs.registerLanguage('fin', require('./fin.js'));
if (typeof module !== 'undefined' && module.exports) {
  module.exports = fin;
} else if (typeof hljs !== 'undefined') {
  hljs.registerLanguage('fin', fin);
}"""
    return (tmpl.replace("@@KEYWORDS@@", keywords)
            .replace("@@OPERATORS@@", json.dumps("(?:" + operators + ")"))
            .replace("@@DIRECTIVES@@", directives))



PYGMENTS_TOKEN = {
    "keyword.control": "Keyword.Control",
    "keyword.declaration": "Keyword.Declaration",
    "keyword.modifier": "Keyword.Declaration",
    "keyword.other": "Keyword",
    "keyword.operator.word": "Operator.Word",
    "storage.type": "Keyword.Type",
    "constant.language": "Keyword.Constant",
    "variable.language": "Name.Builtin.Pseudo",
}


def pygments_token(scope):
    for prefix, token in PYGMENTS_TOKEN.items():
        if scope.startswith(prefix):
            return token
    raise SystemExit(f"no Pygments token for scope {scope!r}")


def pygments(data):
    rules = {}
    for entry in data["types"] + data["builtin_types"] + data["keywords"]:
        rules.setdefault(pygments_token(entry["scope"]), []).append(entry["word"])
    lines = []
    for token in sorted(rules):
        lines.append(
            f"            (r'\\b(?:{alternation(sorted(rules[token]))})\\b',"
            f" {token}),")
    keywords = "\n".join(lines)
    ops = alternation(data["operators"])
    punct = alternation(data["punctuation"])
    directives = "(?:" + alternation(
        [e["word"] for e in data["directives"]]) + r")\b"
    return f'''# SPDX-License-Identifier: GPL-3.0-or-later
# GENERATED by editors/scripts/generate.py from editors/fin-keywords.json --
# do not edit.
#
# Block comments nest through Pygments' state stack, like lexer.l does.

import re

from pygments.lexer import RegexLexer, bygroups
from pygments.token import (
    Comment, Keyword, Name, Number, Operator, Punctuation, String, Whitespace,
)


class FinLexer(RegexLexer):
    """Fin (see src/lexer/lexer.l)."""

    name = "Fin"
    url = "https://github.com/M1778/Fin"
    aliases = ["fin"]
    filenames = ["*.fin"]
    mimetypes = ["text/x-fin"]
    flags = re.MULTILINE

    tokens = {{
        "root": [
            (r"\\s+", Whitespace),
            (r"//.*?$", Comment.Single),
            (r"/\\*", Comment.Multiline, "block-comment"),
            (r"^\\s*#(?:c_[a-z]+|cdef)\\b.*$", Comment.Preproc),
            (r"#\\[", Comment.Preproc, "attribute"),
            (r"%\\{{", Comment.Preproc, "attribute-block"),
            (r"(@)(macro|special|define)\\b(\\s*)([A-Za-z_]\\w*)?",
             bygroups(Keyword, Keyword, Whitespace, Name.Function)),
            (r"\\b(fun)(\\?)?(\\s+)([A-Za-z_]\\w*)",
             bygroups(Keyword.Declaration, Operator, Whitespace, Name.Function)),
            (r"{directives}", Keyword.Reserved),
{keywords}
            (r'"(?:[^\\\\"]|\\\\.)*"', String),
            (r"'[^'\\\\]'", String.Char),
            (r"'\\\\.'", String.Char),
            (r"\\b\\d+\\.\\d+\\b", Number.Float),
            (r"\\b\\d+\\b", Number.Integer),
            (r"(?:{ops})", Operator),
            (r"(?:{punct})", Punctuation),
            (r"[A-Za-z_]\\w*", Name),
        ],
        "block-comment": [
            (r"[^*/]+", Comment.Multiline),
            (r"/\\*", Comment.Multiline, "#push"),
            (r"\\*/", Comment.Multiline, "#pop"),
            (r"[*/]", Comment.Multiline),
        ],
        "attribute": [
            (r"\\]", Comment.Preproc, "#pop"),
            (r'"(?:[^\\\\"]|\\\\.)*"', String),
            (r"\\d+", Number),
            (r"\\s+", Whitespace),
            (r"[\\w.$]+", Name.Attribute),
            (r".", Comment.Preproc),
        ],
        "attribute-block": [
            (r"\\}}%", Comment.Preproc, "#pop"),
            (r"%\\{{", Comment.Preproc, "#push"),
            (r"\\s+", Whitespace),
            (r"[^%}}]+", Comment.Preproc),
            (r".", Comment.Preproc),
        ],
    }}
'''


def main():
    rules = read_lexer()
    keywords, types, directives = build_keywords(rules)
    operators, punctuation = build_symbols(rules)
    data = {
        "_generated": "by editors/scripts/generate.py -- do not edit",
        "language": "fin",
        "scopeName": "source.fin",
        "extensions": ["fin"],
        "sources": {
            "keywords": "src/lexer/lexer.l",
            "builtin_types": "src/semantics/impl/Analyzer_Core.cpp",
            "crosschecked_against": "lib/std/token.fin",
        },
        "keywords": keywords,
        "types": types,
        "builtin_types": build_builtin_types(),
        "directives": directives,
        "operators": operators,
        "punctuation": punctuation,
    }
    (OUT / "fin-keywords.json").write_text(
        json.dumps(data, indent=2) + "\n", encoding="utf-8")
    (OUT / "textmate" / "fin.tmLanguage.json").write_text(
        json.dumps(tm_language(data), indent=2) + "\n", encoding="utf-8")
    (OUT / "highlightjs" / "fin.js").write_text(
        highlightjs(data), encoding="utf-8")
    (OUT / "pygments" / "fin_lexer.py").write_text(
        pygments(data), encoding="utf-8")
    print(f"wrote {OUT.relative_to(ROOT)}/fin-keywords.json "
          f"({len(keywords)} keywords, {len(types)} types, "
          f"{len(data['builtin_types'])} builtin types, "
          f"{len(directives)} directives, {len(operators)} operators, "
          f"{len(punctuation)} punctuation)")


if __name__ == "__main__":
    main()
