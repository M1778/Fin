#!/usr/bin/env python3
# =============================================================================
# Fin Programming Language Compiler
#
# Made with ❤️ for someone special.
#
# This project is genuinely built on love and dedication, a way to express your love can be done through Fin.
# Fin is a semi-object-orientated programming language that specialized in control, giving so much control to the user while asking for the bare minimum.
# Fin is the name of the Angel from BlackNightmare-Stories not yet published.
# “Love is the reason this project exists”
# -----------------------------------------------------------------------------
# Author: M1778
# Repository: https://github.com/M1778/Fin
# Profile: https://github.com/M1778M/
# -----------------------------------------------------------------------------
# Copyright (C) 2027 M1778
#
# This file is part of the Fin Programming Language Compiler.
#
# Fin is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Fin is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Fin.  If not, see <https://www.gnu.org/licenses/>.
#
# SPDX-License-Identifier: GPL-3.0-or-later
# =============================================================================

"""Machine-verifiable proof for the editor grammars in editors/.

Checks, in order:
  1. every JSON artifact parses;
  2. the TextMate grammar compiles under Python `re`, every `include` resolves
     to a repository rule or `$self`/`$base`, and each repository value is one
     rule object; with vscode-textmate installed it is also replayed through
     the real Oniguruma engine (scopes, comment nesting, attribute/preprocessor
     shapes);
  3. editors/fin-keywords.json re-derives exactly from src/lexer/lexer.l
     (keywords, types, directives, operators, punctuation) and agrees with
     lib/std/token.fin except for the two documented deltas;
  4. all four artifacts are byte-identical to a fresh run of their generator;
  5. the highlight.js file loads under a stub of the stable `hljs` interface and,
     when highlight.js itself is installed, highlights a real sample;
  6. if Pygments is importable, a real sample tokenizes: at least one keyword
     and one string, and every JSON word lands in its mapped token.

Missing optional engines are reported as `skip`, never as a pass.

Run:  python3 editors/scripts/check_editors.py
      uv run --no-project --with pygments python3 editors/scripts/check_editors.py
      NODE_PATH=/tmp/opencode/node_modules python3 editors/scripts/check_editors.py
Exit status 0 means every check passed.
"""

import importlib.util
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# The checker imports the generator and the lexer; keep __pycache__ out of the
# tree so `git status` shows only the artifacts.
sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "editors"
TOKENS = ROOT / "lib/std/token.fin"

# lexer.l and lib/std/token.fin disagree on exactly these two points, both
# documented in README.md. lexer.l owns the tokens the compiler emits;
# token.fin is the shipped library's own table. The checker fails if either set
# changes in either direction, so a repair or a new divergence cannot pass
# unnoticed.
WORD_DELTA = {"match"}   # in lexer.l and parser.y, absent from token.fin
KIND_DELTA = {"noret"}   # lexer.l emits TYPE_VOID; token.fin names TYPE_NORET

failures = []


def check(condition, message):
    if condition:
        print(f"  ok   {message}")
    else:
        print(f"  FAIL {message}")
        failures.append(message)


def load_generator():
    path = OUT / "scripts" / "generate.py"
    spec = importlib.util.spec_from_file_location("fin_editors_generate", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def parse_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def re_compiles(pattern):
    try:
        re.compile(pattern)
        return True
    except re.error:
        return False


def walk_rules(node):
    """Yield every rule dict in a TextMate grammar (patterns and repository)."""
    if isinstance(node, dict):
        if "match" in node or "begin" in node or "include" in node:
            yield node
        for value in node.values():
            yield from walk_rules(value)
    elif isinstance(node, list):
        for item in node:
            yield from walk_rules(item)


def check_textmate(grammar):
    check(grammar.get("scopeName") == "source.fin", "scopeName is source.fin")
    check(isinstance(grammar.get("patterns"), list) and grammar["patterns"],
          "grammar has non-empty patterns")
    repository = grammar.get("repository", {})
    check(isinstance(repository, dict) and repository,
          "grammar has a repository")
    # A repository value must be ONE rule object. vscode-textmate silently
    # ignores an array here, which is how a grammar loses every rule in it.
    arrays = [name for name, rule in repository.items()
              if not isinstance(rule, dict)]
    check(not arrays,
          "every repository value is a single rule object, not a list"
          + (f" (arrays: {arrays})" if arrays else ""))

    includes, bad_includes, bad_regex, compiled = [], [], [], 0
    for rule in walk_rules({"patterns": grammar["patterns"],
                            "repository": repository}):
        for key in ("match", "begin", "end"):
            if key in rule:
                compiled += 1
                if not re_compiles(rule[key]):
                    bad_regex.append(rule[key])
        if "include" in rule:
            includes.append(rule["include"])
    for target in includes:
        if target in ("$self", "$base"):
            continue
        if not (target.startswith("#") and target[1:] in repository):
            bad_includes.append(target)

    check(not bad_regex,
          f"all {compiled} TextMate regexes compile in Python re"
          + (f" (bad: {bad_regex})" if bad_regex else ""))
    check(not bad_includes,
          f"all {len(includes)} include references resolve"
          + (f" (bad: {bad_includes})" if bad_includes else ""))
    names = [r.get("name", "") for r in walk_rules(
        {"patterns": grammar["patterns"], "repository": repository})]
    standard = ("keyword.", "storage.type", "entity.name.function",
                "constant.", "comment.", "string.", "punctuation.",
                "variable.other", "meta.")
    check(any(n.startswith(standard) for n in names if n),
          "scope names use standard TextMate conventions")


def check_derivation(data):
    gen = load_generator()
    rules = gen.read_lexer()
    keywords, types, directives = gen.build_keywords(rules)
    operators, punctuation = gen.build_symbols(rules)
    builtin = gen.build_builtin_types()

    for field, expected in (("keywords", keywords), ("types", types),
                            ("directives", directives),
                            ("builtin_types", builtin)):
        check(data[field] == expected,
              f"fin-keywords.json {field} re-derives from lexer.l "
              f"({len(expected)} entries)")
    check(data["operators"] == operators,
          f"operators re-derive from lexer.l ({len(operators)} symbols)")
    check(data["punctuation"] == punctuation,
          f"punctuation re-derives from lexer.l ({len(punctuation)} symbols)")

    # lexer words vs the shipped library's keyword table.
    token_source = TOKENS.read_text(encoding="utf-8")
    lookup = dict(re.findall(
        r'tok_str_eq\(ident, "([^"]+)"\)\) \{ return (\d+);', token_source))
    num_name = {int(n): c for c, n in re.findall(
        r'pub const (KW_\w+|TYPE_\w+|TOKEN_\w+) <int> = (\d+);', token_source)}
    lexer_words = {w for w, _ in rules if gen.is_word(w)}
    json_words = {e["word"] for e in keywords + types}
    check(json_words == lexer_words,
          f"JSON word set equals lexer.l's word set ({len(lexer_words)} words)")
    word_delta = lexer_words.symmetric_difference(lookup)
    check(word_delta == WORD_DELTA,
          f"lexer.l vs lib/std/token.fin word delta is exactly {sorted(WORD_DELTA)}"
          + (f" (got {sorted(word_delta)})" if word_delta != WORD_DELTA else ""))
    kind_delta = set()
    for word in lexer_words & set(lookup):
        kind = dict((w, k) for w, k in rules)[word]
        declared = num_name.get(int(lookup[word]))
        if declared is not None and declared != kind:
            kind_delta.add(word)
    check(kind_delta == KIND_DELTA,
          f"lexer.l vs lib/std/token.fin kind-name delta is exactly "
          f"{sorted(KIND_DELTA)}" + (f" (got {sorted(kind_delta)})"
                                      if kind_delta != KIND_DELTA else ""))

    # Every word's token kind must agree with token.fin where both name it.
    mismatched = []
    for entry in keywords + types:
        word, kind = entry["word"], entry["token"]
        declared = num_name.get(int(lookup.get(word, -1)))
        if declared is None or word in KIND_DELTA:
            continue
        if declared != kind:
            mismatched.append((word, kind, declared))
    check(not mismatched,
          "every keyword's token kind agrees with token.fin"
          + (f" (bad: {mismatched})" if mismatched else ""))


def check_artifacts_match_generator(data, grammar, hljs_text, pygments_text):
    gen = load_generator()
    check(json.dumps(gen.tm_language(data), indent=2) + "\n"
          == (OUT / "textmate" / "fin.tmLanguage.json").read_text("utf-8"),
          "textmate/fin.tmLanguage.json is in sync with generate.py")
    check(gen.highlightjs(data) == hljs_text,
          "highlightjs/fin.js is in sync with generate.py")
    check(gen.pygments(data) == pygments_text,
          "pygments/fin_lexer.py is in sync with generate.py")


HLJS_STUB = """
const vm = require('vm');
const fs = require('fs');
const path = process.argv[2];
const rules = [];
const identity = (name) => ({ name });
const hljs = {
  C_LINE_COMMENT_MODE: identity('comment.line'),
  C_BLOCK_COMMENT_MODE: identity('comment.block'),
  NUMBER_MODE: identity('number'),
  QUOTE_STRING_MODE: identity('string'),
  registerLanguage(name, fn) { rules.push([name, fn]); },
};
const definition = require(path);
process.stdout.write(JSON.stringify({
  name: definition(hljs).name,
  keywords: definition(hljs).keywords,
  rules: rules.length,
}));

// Browser path: no CommonJS, a global hljs, script plain-evaluated.
const source = fs.readFileSync(path, 'utf8');
const context = { hljs: { ...hljs, registerLanguage(n, f) { rules.push([n, f]); } } };
context.globalThis = context;
vm.createContext(context);
vm.runInContext(source, context);
process.stdout.write('\\n' + JSON.stringify(rules.map((r) => r[0])));
"""

# Real highlight.js, used only if the caller made it importable (uv/npm is not
# a build dependency of this checker).
HLJS_REAL = """
let hljs;
try { hljs = require('highlight.js'); } catch (e) { process.exit(3); }
hljs.registerLanguage('fin', require(process.argv[2]));
const src = require('fs').readFileSync(process.argv[3], 'utf8');
const out = hljs.highlight(src, { language: 'fin' });
process.stdout.write(JSON.stringify({
  name: hljs.getLanguage('fin').name,
  html: out.value,
}));
"""


def check_highlightjs(hljs_text, data):
    check("module.exports = fin" in hljs_text,
          "highlight.js file exports its definition (module.exports)")
    check("hljs.registerLanguage('fin'" in hljs_text,
          "highlight.js file self-registers when loaded as a plain script")
    # Only the stable interface is used: every hljs.* name is one of these.
    used = set(re.findall(r"hljs\.(\w+)", hljs_text))
    allowed = {"C_LINE_COMMENT_MODE", "C_BLOCK_COMMENT_MODE", "NUMBER_MODE",
               "QUOTE_STRING_MODE", "registerLanguage"}
    check(used <= allowed,
          f"highlight.js uses only the stable interface (used: {sorted(used)})")

    node = shutil.which("node")
    if not node:
        print("  skip node absent, cannot execute the highlight.js file")
        return
    with tempfile.TemporaryDirectory() as tmp:
        stub = Path(tmp) / "stub.js"
        stub.write_text(HLJS_STUB, encoding="utf-8")
        proc = subprocess.run(
            [node, str(stub), str(OUT / "highlightjs" / "fin.js")],
            capture_output=True, text=True)
        (Path(tmp) / "real.js").write_text(HLJS_REAL, encoding="utf-8")
        real = Path(tmp) / "real.js"
        check(proc.returncode == 0,
              "highlight.js file loads under a stub of the stable interface"
              + (f" ({proc.stderr.strip().splitlines()[-1]})"
                 if proc.returncode else ""))
        if proc.returncode:
            return
        loaded, registered = proc.stdout.strip().split("\n")
        loaded = json.loads(loaded)
        registered = json.loads(registered)
        check(loaded["name"] == "Fin",
              "exported definition names the language Fin")
        check(registered == ["fin"],
              "browser path registers exactly one language: fin")

        # Every JSON keyword must be in a group the definition will use.
        placed = set()
        for words in loaded["keywords"].values():
            placed |= set(words)
        expected = {e["word"] for e in data["keywords"] + data["types"]
                    + data["builtin_types"]}
        check(expected <= placed,
              f"all {len(expected)} keyword JSON words are in the hljs definition"
              + (f" (missing: {sorted(expected - placed)})"
                 if expected - placed else ""))

        # Real engine, if the caller supplied highlight.js on NODE_PATH.
        samples = {
            "tests/samples/nullifier.fin": ("hljs-keyword", "hljs-type",
                                            "hljs-comment", "hljs-literal"),
            "tests/samples/prototype_test.fin": ("hljs-string", "hljs-number"),
        }
        for relative, scopes in samples.items():
            proc = subprocess.run(
                [node, str(real), str(OUT / "highlightjs" / "fin.js"),
                 str(ROOT / relative)],
                capture_output=True, text=True)
            if proc.returncode == 3:
                print("  skip real highlight.js not installed; install and "
                      "rerun with:\n"
                      "       npm --prefix /tmp/opencode i highlight.js\n"
                      "       NODE_PATH=/tmp/opencode/node_modules "
                      "python3 editors/scripts/check_editors.py")
                return
            check(proc.returncode == 0,
                  f"highlights {relative} under real highlight.js"
                  + (f" ({proc.stderr.strip().splitlines()[-1]})"
                     if proc.returncode else ""))
            if proc.returncode:
                return
            result = json.loads(proc.stdout)
            check(result["name"] == "Fin", "real highlight.js registered Fin")
            for scope in scopes:
                check(scope in result["html"],
                      f"real highlight.js emits {scope} in {relative}")


# Real engine probe. Emits the scopes each line produces plus two shape checks
# that only the real (nested-capable) state machine can answer.
TM_REAL = """
const fs = require('fs');
const vsctm = require('vscode-textmate');
const oniguruma = require('vscode-oniguruma');

const wasm = fs.readFileSync(
  require.resolve('vscode-oniguruma/release/onig.wasm')).buffer;
const onigLib = oniguruma.loadWASM(wasm).then(() => ({
  createOnigScanner: (scanners) => new oniguruma.OnigScanner(scanners),
  createOnigString: (text) => new oniguruma.OnigString(text),
}));

const grammarPath = process.argv[2];
const registry = new vsctm.Registry({
  onigLib,
  loadGrammar: async (scope) => scope === 'source.fin'
    ? vsctm.parseRawGrammar(fs.readFileSync(grammarPath, 'utf8'), grammarPath)
    : null,
});

function scopesOf(grammar, lines) {
  let stack = vsctm.INITIAL;
  const found = new Set();
  for (const line of lines) {
    const { tokens, ruleStack } = grammar.tokenizeLine(line, stack);
    stack = ruleStack;
    for (const token of tokens) for (const scope of token.scopes) found.add(scope);
  }
  return found;
}

function scopeAt(grammar, line, needle) {
  const tokens = grammar.tokenizeLine(line, vsctm.INITIAL).tokens;
  const at = line.indexOf(needle);
  const token = tokens.find((t) => t.startIndex <= at && at < t.endIndex);
  return token ? token.scopes.slice(1).join(' ') : null;
}

registry.loadGrammar('source.fin').then((grammar) => {
  const scopes = new Set();
  for (const file of process.argv.slice(3))
    for (const scope of scopesOf(grammar, fs.readFileSync(file, 'utf8').split('\\n')))
      scopes.add(scope);

  const nestedLine = '/* outer /* inner */ still outer */';
  const nestedTokens = grammar.tokenizeLine(nestedLine, vsctm.INITIAL).tokens;
  const escapeTokens = grammar.tokenizeLine(
    'let s <string> = "a\\\\nb";', vsctm.INITIAL).tokens;
  const escape = escapeTokens.some(
    (t) => t.scopes.includes('constant.character.escape.fin'));
  const depth = (needle) => {
    const at = nestedLine.indexOf(needle);
    const token = nestedTokens.find((t) => t.startIndex <= at && at < t.endIndex);
    return token ? token.scopes.filter((s) => s === 'comment.block.fin').length : 0;
  };

  process.stdout.write(JSON.stringify({
    scopes: [...scopes].sort(),
    inner_depth: depth('inner'),
    outer_depth: depth('still outer'),
    string_escape: escape ? 'constant.character.escape.fin' : null,
    shapes: {
      attribute: scopeAt(grammar, '#[llvm_name="free"] @define f() <noret>;', 'llvm_name'),
      attribute_block: scopeAt(grammar, '%{ pub type i32 = int; }%', 'pub type'),
      preprocessor: scopeAt(grammar, '    #c_ifdef FIN_TARGET_LINUX', '#c_ifdef'),
      directive: scopeAt(grammar, '    #for x', '#for'),
      macro: scopeAt(grammar, '@macro twice(v) {}', 'twice'),
      macro_call: scopeAt(grammar, 'blame twice!(6) == 12;', 'twice'),
      nullable_fun: scopeAt(grammar, 'fun? maybe(n?: int) <A> {}', '?'),
      type_decl: scopeAt(grammar, 'struct Data <T> {}', 'Data'),
      char: scopeAt(grammar, "'\\\\n'", "'"),
    },
  }));
});
"""


def check_textmate_engine(grammar):
    """Real Oniguruma/vscode-textmate replay, when the caller supplied it."""
    node = shutil.which("node")
    if not node:
        return
    name = "vscode-textmate"
    probe = subprocess.run(
        [node, "-e", f"try{{require.resolve('{name}')}}catch(e){{process.exit(3)}}"],
        capture_output=True, text=True)
    if probe.returncode != 0:
        print("  skip vscode-textmate not installed; install and rerun with:\n"
              "       npm --prefix /tmp/opencode i vscode-textmate "
              "vscode-oniguruma\n"
              "       NODE_PATH=/tmp/opencode/node_modules "
              "python3 editors/scripts/check_editors.py")
        return
    with tempfile.TemporaryDirectory() as tmp:
        script = Path(tmp) / "tm.js"
        script.write_text(TM_REAL, encoding="utf-8")
        proc = subprocess.run(
            [node, str(script), str(OUT / "textmate" / "fin.tmLanguage.json"),
             str(ROOT / "tests/samples/nullifier.fin"),
             str(ROOT / "tests/samples/prototype_test.fin")],
            capture_output=True, text=True)
    check(proc.returncode == 0,
          "grammar loads in the real vscode-textmate engine"
          + (f" ({proc.stderr.strip().splitlines()[-1]})"
             if proc.returncode else ""))
    if proc.returncode:
        return
    results = json.loads(proc.stdout)
    required = {
        "source.fin", "comment.line.double-slash.fin",
        "keyword.declaration.fin", "keyword.control.fin",
        "storage.type.fin", "keyword.operator.fin", "punctuation.fin",
        "string.quoted.double.fin", "constant.numeric.integer.fin",
        "entity.name.function.fin",
    }
    seen = set(results["scopes"])
    check(required <= seen,
          "real engine produces every expected scope on the corpus"
          + (f" (missing: {sorted(required - seen)})" if required - seen else ""))
    check(results["inner_depth"] == 2 and results["outer_depth"] == 1,
          "real engine nests block comments like lexer.l "
          f"(inner depth {results['inner_depth']}, outer depth "
          f"{results['outer_depth']})")
    check(results["string_escape"] == "constant.character.escape.fin",
          "real engine scopes an escape inside a string")
    shapes = results["shapes"]
    expected_shapes = {
        "attribute": "meta.attribute.fin",
        "attribute_block": "meta.attribute-block.fin",
        "preprocessor": "keyword.control.preprocessor.fin",
        "directive": "keyword.directive.fin",
        "macro": "meta.macro-declaration.fin",
        "macro_call": "meta.macro-call.fin",
        "nullable_fun": "keyword.operator.nullable.fin",
        "type_decl": "entity.name.type.fin",
        "char": "string.quoted.single.fin",
    }
    for shape, scope in expected_shapes.items():
        check(scope in (shapes.get(shape) or ""),
              f"real engine scopes {shape} as {scope}"
              + (f" (got {shapes.get(shape)!r})"
                 if scope not in (shapes.get(shape) or "") else ""))


def check_pygments(data):
    try:
        sys.path.insert(0, str(OUT / "pygments"))
        from fin_lexer import FinLexer
        from pygments import lex
        from pygments import token as pygments_token_module
        from pygments.token import Comment, Keyword, Operator, String

        def resolve(name):
            node = pygments_token_module
            for part in name.split("."):
                node = getattr(node, part)
            return node
    except ImportError as exc:
        print(f"  skip Pygments unavailable ({exc}); "
              "run with: uv run --no-project --with pygments ...")
        return

    sample = (ROOT / "tests/samples/blame_assert.fin").read_text("utf-8")
    tokens = list(lex(sample, FinLexer()))
    check(bool(tokens), "Pygments tokenizes tests/samples/blame_assert.fin")
    keyword_tokens = [v for t, v in tokens if t in Keyword or t is Operator.Word]
    string_tokens = [v for t, v in tokens if t in String]
    comment_tokens = [v for t, v in tokens if t in Comment]
    check(keyword_tokens, f"a keyword token is classified "
                          f"({keyword_tokens[:4]})")
    check(string_tokens, f"a string token is classified ({string_tokens[:2]})")
    check(comment_tokens, "a comment token is classified")

    nested = list(lex("/* outer /* inner */ still outer */ fun x() {}", FinLexer()))
    still = [v for t, v in nested if t in Comment.Multiline]
    check(any("still outer" in v for v in still),
          "block comments nest like lexer.l (inner close does not end outer)")

    # Every JSON word must classify as the token its scope maps to, derived
    # through the generator's own scope -> Pygments token table.
    gen = load_generator()
    misclassified = []
    entries = data["keywords"] + data["types"] + data["builtin_types"]
    for entry in entries:
        expected = resolve(gen.pygments_token(entry["scope"]))
        got = list(lex(entry["word"], FinLexer()))
        if not any(t is expected for t, _ in got):
            misclassified.append((entry["word"], expected, got))
    check(not misclassified,
          f"all {len(entries)} JSON words classify as their mapped Pygments token"
          + (f" (bad: {misclassified[:5]})" if misclassified else ""))

    print("\n  sample tokenization (first 24 of "
          f"{len(tokens)} tokens):")
    for ttype, value in tokens[:24]:
        print(f"    {str(ttype):<28} {value!r}")


def main():
    print("1. JSON artifacts parse")
    data = parse_json(OUT / "fin-keywords.json")
    grammar = parse_json(OUT / "textmate" / "fin.tmLanguage.json")
    check(True, "fin-keywords.json parses")
    check(True, "textmate/fin.tmLanguage.json parses")

    print("\n2. TextMate grammar validity")
    check_textmate(grammar)
    check_textmate_engine(grammar)

    print("\n3. Keyword derivation")
    check_derivation(data)

    print("\n4. Artifacts match their generator")
    check_artifacts_match_generator(
        data, grammar,
        (OUT / "highlightjs" / "fin.js").read_text(encoding="utf-8"),
        (OUT / "pygments" / "fin_lexer.py").read_text(encoding="utf-8"))

    print("\n5. highlight.js")
    check_highlightjs((OUT / "highlightjs" / "fin.js").read_text("utf-8"), data)

    print("\n6. Pygments")
    check_pygments(data)

    print()
    if failures:
        print(f"FAILED: {len(failures)} check(s)")
        return 1
    print("PASS: all editor artifacts verified")
    return 0


if __name__ == "__main__":
    sys.exit(main())
