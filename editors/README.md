# Fin editor grammars

Syntax highlighting for Fin, generated from the compiler's own lexer. Four
consumers, one source of truth:

| File | Consumer |
| --- | --- |
| `fin-keywords.json` | the single word list all three grammars read |
| `textmate/fin.tmLanguage.json` | VS Code, Neovim, TextMate, and anything else that speaks TextMate |
| `highlightjs/fin.js` | highlight.js (web pages, docs sites) |
| `pygments/fin_lexer.py` | Pygments (Sphinx, mkdocs, Jupyter, `pygmentize`) |

`fin-vscode` (a separate repository) is the eventual packaged extension. It
consumes `textmate/fin.tmLanguage.json` from here rather than carrying its own
copy, so a keyword added to the compiler reaches every editor in one change.

## Where the words come from

Nothing here is invented. `scripts/generate.py` reads:

- `src/lexer/lexer.l` — every `"word" { MAKE(KIND); }` rule is a keyword, type,
  or directive; every non-word rule is an operator or a punctuation mark.
- `lib/std/token.fin` — the shipped library's own token table, cross-checked
  against the lexer. The checker fails if the two disagree beyond the
  documented deltas below.
- `src/semantics/impl/Analyzer_Core.cpp` — the primitive type names the
  semantic layer registers as builtins (`uint`, `object`, `i8`, `usize`, ...).
  These are not reserved lexer words, but they are types an editor should
  colour.

Comment and string syntax follows `lexer.l`: `//` to end of line, `/* ... */`
with **nesting** (an inner `/*` opens a level, so a commented-out region that
itself contains a comment closes where it was written), `"..."` strings with
`\`-escapes, and `'x'` / `'\n'` character literals.

### Known deltas between `lexer.l` and `lib/std/token.fin`

Both are recorded here instead of being silently resolved:

1. **`match`** is a keyword in `lexer.l` and `src/parser/parser.y`, and is
   **absent from `lib/std/token.fin`'s `lookup_keyword`**. The grammars treat it
   as a keyword, because the compiler does.
2. **`noret`** is emitted by `lexer.l` as `TYPE_VOID`; `token.fin` names the
   kind `TYPE_NORET` (number 78). Same word, two names for the kind.

`check_editors.py` asserts these are *exactly* the deltas, in both directions, so
a repair or a new divergence fails the build.

## Regenerating

```sh
python3 editors/scripts/generate.py    # rewrites all four artifacts
python3 editors/scripts/check_editors.py
```

The artifacts are committed. Edit `generate.py` (or the `CATEGORIES` map that
decides which scope a word reads as), never the generated files. A new keyword
in `lexer.l` fails `generate.py` loudly until `CATEGORIES` places it.

## Verifying

```sh
# Everything that needs no third-party package:
python3 editors/scripts/check_editors.py

# With the real engines (installed into a scratch directory of your choice):
npm --prefix "$SCRATCH" i highlight.js vscode-textmate vscode-oniguruma
NODE_PATH="$SCRATCH/node_modules" python3 editors/scripts/check_editors.py
```

What the checker proves: the JSON parses; every TextMate regex compiles and
every `include` resolves; each TextMate `repository` value is a single rule
object (the engine silently ignores an array); the keyword list re-derives
exactly from `lexer.l`; all four artifacts are byte-identical to a fresh run of
the generator; the highlight.js file loads and registers `fin`; and, when the
engines are present, real `vscode-textmate` and real `highlight.js` highlight
`tests/samples/nullifier.fin` with the expected scopes, and Pygments maps every
JSON word to its declared token. A missing optional engine prints `skip`; it
never counts as a pass.

## VS Code

Install the grammar as a local extension until `fin-vscode` ships:

```sh
mkdir -p ~/.vscode/extensions/fin-local
cp editors/textmate/fin.tmLanguage.json ~/.vscode/extensions/fin-local/
cat > ~/.vscode/extensions/fin-local/package.json <<'EOF'
{
  "name": "fin-local",
  "version": "0.0.0",
  "engines": { "vscode": "^1.60.0" },
  "contributes": {
    "languages": [{ "id": "fin", "extensions": [".fin"], "aliases": ["Fin"] }],
    "grammars": [{
      "language": "fin",
      "scopeName": "source.fin",
      "path": "./fin.tmLanguage.json"
    }]
  }
}
EOF
```

Reload the window. `source.fin` is the scope name every theme already keys on
`keyword.control`, `storage.type`, `entity.name.function`, and friends, so no
theme work is needed.

## Neovim

Neovim has no built-in TextMate engine, so pick one of two paths.

**TextMate (same grammar as VS Code).** Load
`editors/textmate/fin.tmLanguage.json` with a TextMate-to-Neovim loader
(`nvim-tm`, or `ts-utils`' TextMate support) and map `*.fin` to the filetype:

```lua
-- ~/.config/nvim/after/ftdetect/fin.lua
vim.filetype.add({ extension = { fin = "fin" } })
```

Point the loader at the file above and at `source.fin`; every scope in the map
below is the standard name Neovim links to `Keyword`, `Type`, `Comment`,
`String`, and so on, so no highlight groups are needed.

**tree-sitter.** The repository already carries a grammar and highlight queries
at `tools/grammar/tree-sitter-fin/`; registering that parser gives you
`@keyword`, `@type`, `@string`, `@comment` captures through Neovim's own
highlighter. This directory does not duplicate those queries — both lists come
from the same `fin-keywords.json`, so they stay in step.

Either way, `*.fin` is not a filetype Neovim knows, so the `ftdetect` line is
what starts highlighting.

## Pygments

```sh
pygmentize -x -l ./editors/pygments/fin_lexer.py:FinLexer \
    -f terminal256 tests/samples/nullifier.fin
```

`-x` loads the file directly; the class is `FinLexer` (`name = "Fin"`, alias
`fin`, glob `*.fin`). Pygments only discovers the `fin` alias automatically
after the lexer is installed with an entry point, so without `-x` (or
`PYTHONPATH` plus your own plugin registration) `pygmentize -N` reports `text`.
As a module:

```python
from fin_lexer import FinLexer
from pygments import lex
print(list(lex("fun main() <noret> {}", FinLexer())))
```

**Limitation:** Pygments' regex states cannot count nesting depth. This lexer
uses its state stack (`#push`/`#pop`) so nested `/* /* */ */` does close
correctly, but a malformed comment can leave the stack open to end of file.

## highlight.js

```html
<pre><code class="language-fin">fun main() &lt;noret&gt; { blame 1 == 1; }</code></pre>
<script src="https://cdn.jsdelivr.net/npm/highlight.js@11/lib/common.min.js"></script>
<script src="editors/highlightjs/fin.js"></script>
<script>hljs.highlightAll();</script>
```

Loaded as a classic script it registers itself. As a module:

```js
const hljs = require('highlight.js');
hljs.registerLanguage('fin', require('./editors/highlightjs/fin.js'));
```

**Limitation:** highlight.js has no nested block-comment state, so `/* /* */ */`
closes at the first `*/`. `lexer.l` nests. Every other construct maps
one-to-one.

## Scope map

The TextMate scopes are the standard ones, so existing themes colour Fin without
a theme rule:

| Scope | Fin |
| --- | --- |
| `keyword.control` | `if else while do for foreach match break continue return try catch blame` |
| `keyword.declaration` | `let const fun struct enum interface namespace macro static operator type class implements special define extern import from delete` |
| `keyword.modifier` | `pub priv readonly` |
| `keyword.operator.word` | `new sizeof typeof as_ptr cast in` |
| `keyword.other` | `as quote` |
| `storage.type` | `int long float double bool string char void noret`, plus `any Self auto fn` and the builtin aliases (`uint ulong short ushort object i8..u64 usize isize size_t f32 f64`) |
| `constant.language` | `null true false m1778` |
| `variable.language` | `super` |
| `comment.line.double-slash` / `comment.block` | `//`, `/* */` (nesting) |
| `string.quoted.double` / `string.quoted.single` | `"..."`, `'x'` |
| `constant.numeric.integer` / `.float` | `10`, `1.5` |
| `entity.name.function` | the name in `fun name`, `@macro name`, `@special name`, `@define name` |
| `entity.name.type` | the name in `struct Name`, `enum Name`, `interface Name`, `class Name`, `type Name` |
| `entity.name.function.macro` | the name in `name!(...)` |
| `meta.attribute` | `#[export]`, `#[llvm_name="sym"]`, `#[use(...)]` |
| `meta.attribute-block` | `%{ ... }%` |
| `keyword.control.preprocessor` | `#cdef`, `#c_ifdef`, `#c_else`, `#c_endif` |
| `keyword.directive` | `#for`, `#index` |

`m1778` rides with `blame` because `blame m1778;` is one idiom; `super` is a
language pseudo-variable, not a declaration keyword.

`fin-keywords.json` also tags each word with `form` — `statement`, `expression`,
`modifier`, or `type` — so a consumer that needs statement-introducing keywords
(an indent rule, a folding rule) reads that field instead of re-deriving it.
