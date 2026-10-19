#!/usr/bin/env node
// Validates tools/grammar/fin.tmLanguage.json by replaying it over a real Fin
// snippet assembled from the five normative samples lane 2 must parse
// (tests/samples/{basic,structs,functions,loops,importing}.fin) plus real
// lines from lib/std (`#[stdimport] %{ ... }%`, `#[llvm_name=...]`, char
// literals) and the two lexer-normative constructs no sample currently writes
// (nestable block comments, `blame m1778;`).
//
// Proof given here: JSON validity, every pattern/begin/end regex compiling,
// single-source keyword survival, and scope-event assertions through the
// replay. The replay approximates vscode-textmate: at a position the first
// rule in ORDER wins; a begin/end region consumes up to its end token with
// its inner patterns running in between. It is not the real engine
// (Oniguruma vs JS regex) -- the generated grammar sticks to the portable
// regex subset, and the real oniguruma proof happens when the vsix is built
// in M1778/fin-vscode.

const fs = require("fs");
const path = require("path");

const ROOT = path.resolve(__dirname, "..", "..");
const S = (r) => path.join(ROOT, r);
const grammar = JSON.parse(fs.readFileSync(S("tools/grammar/fin.tmLanguage.json"), "utf8"));
if (grammar.scopeName !== "source.fin")
  throw new Error("scopeName is " + grammar.scopeName + ", expected source.fin");

// --- proof 2: every regex compiles ------------------------------------------
let compiled = 0;
function compileAll(rules) {
  for (const rule of rules) {
    for (const key of ["match", "begin", "end"])
      if (rule[key] !== undefined) { new RegExp(rule[key], "y"); compiled++; }
    if (rule.patterns) compileAll(rule.patterns);
  }
}
compileAll(grammar.patterns);
compileAll(Object.values(grammar.repository));
console.log("compiles " + compiled + " regexes across " + grammar.patterns.length +
  " top-level patterns");

// --- proof 3: keywords/types/directives survived into the artifact ------------
const kw = JSON.parse(fs.readFileSync(S("tools/grammar/fin-keywords.json"), "utf8"));
const artifact = fs.readFileSync(S("tools/grammar/fin.tmLanguage.json"), "utf8");
for (const w of [...kw.keywords, ...kw.types, ...kw.directives])
  if (!artifact.includes(w.replace(/^#/, "")))
    throw new Error("single-source violation: " + w + " missing");

// --- snippet: real Fin sources -------------------------------------------------
const snippets = [];
for (const f of ["basic.fin", "structs.fin", "functions.fin", "loops.fin", "importing.fin"])
  snippets.push(fs.readFileSync(S("tests/samples/" + f), "utf8"));
snippets.push(fs.readFileSync(S("lib/std/typing.fin"), "utf8").split("\n").slice(63, 80).join("\n"));
snippets.push(fs.readFileSync(S("lib/std/printer.fin"), "utf8").split("\n")[75] + "\n"); // write_repeat(' ')
snippets.push(fs.readFileSync(S("lib/std/fs.fin"), "utf8").split("\n")[43] + "\n"); // `if (s != null)`
snippets.push(fs.readFileSync(S("tests/samples/deeptest2.fin"), "utf8").split("\n")[14] + "\n"); // &Self operator param
snippets.push(fs.readFileSync(S("tests/samples/deeptest3.fin"), "utf8").split("\n")[124] + "\n"); // &&int reference type
snippets.push("/* nested /* opens again */ still closes at my own */\n");
snippets.push("blame x == y, \"message\";\nblame m1778;\n");
const snippet = snippets.join("\n");

// --- replay -------------------------------------------------------------------
const memo = new Map();
function flatten(patterns) {
  const key = patterns.map((r) => r.name || r.match || r.begin).join("|");
  if (memo.has(key)) return memo.get(key);
  const out = [];
  for (const rule of patterns) {
    if (rule.include) {
      if (rule.include === "#comment") out.push(...flatten(grammar.repository.comment));
      else if (rule.include === "$base") out.push(...flatten(grammar.patterns));
      else throw new Error("unresolved include " + rule.include);
    } else out.push(rule);
    memo.set(key, out);
  }
  return out;
}

const events = [];
function matchAt(rules, text, pos) {
  for (const rule of rules) {
    const re = new RegExp(rule.match !== undefined ? rule.match : rule.begin, "y");
    re.lastIndex = pos;
    const m = re.exec(text);
    if (m) return { rule, m };
  }
  return null;
}

// Consume a begin/end region starting at beginPos (begin already matched).
// Emits the region's own span plus every inner scope it holds.
function consumeRegion(rule, text, beginPos, limit) {
  const endRe = new RegExp(rule.end, "y");
  const inner = flatten(rule.patterns || []);
  // begin length: re-match at beginPos only to measure it
  let p = beginPos + matchAt([rule], text, beginPos).m[0].length;
  for (;;) {
    endRe.lastIndex = p;
    const em = endRe.exec(text);
    if (em) {
      events.push({ scope: rule.name, start: beginPos, end: p + em[0].length, text: text.slice(beginPos, p + em[0].length) });
      return p + em[0].length;
    }
    if (p >= limit) throw new Error("unclosed region " + rule.name + " from offset " + beginPos);
    const hit = matchAt(inner, text, p);
    if (hit) {
      if (hit.rule.end !== undefined) p = consumeRegion(hit.rule, text, p, limit);
      else {
        events.push({ scope: hit.rule.name, start: p, end: p + hit.m[0].length, text: hit.m[0] });
        p += hit.m[0].length;
      }
    } else p++; // a byte inside the region no rule owns (whitespace etc.)
  }
}

function scan(rules, text, from, limit) {
  let pos = from;
  while (pos < limit) {
    if (/\s/.test(text[pos])) { pos++; continue; }
    const hit = matchAt(rules, text, pos);
    if (!hit) throw new Error("no rule at offset " + pos + " for " + JSON.stringify(text.slice(pos, pos + 14)));
    if (hit.rule.end !== undefined) pos = consumeRegion(hit.rule, text, pos, limit);
    else {
      events.push({ scope: hit.rule.name, start: pos, end: pos + hit.m[0].length, text: hit.m[0] });
      pos += hit.m[0].length;
    }
  }
  return pos;
}

scan(flatten(grammar.patterns), snippet, 0, snippet.length);
console.log("replayed " + snippet.length + " bytes -> " + events.length + " scope events");

// --- assertions ------------------------------------------------------------------
const fail = (m) => { console.error("FAIL: " + m); process.exit(1); };
const check = (ok, m) => { if (!ok) fail("assertion: " + m); };
const has = (scope, contains) =>
  events.some((e) => e.scope === scope && String(e.text ?? "").includes(contains));
const inside = (at, scope) => {
  const i = snippet.indexOf(at);
  check(i >= 0, "snippet lacks " + JSON.stringify(at));
  return events.some((e) => e.scope === scope && e.start <= i && i < e.end);
};

// comments: nestable per lexer.l
check(inside("still closes at my own", "comment.block.fin"), "nested block comment lost as comment");
check(inside("opens again", "comment.block.fin"), "inner comment level lost");
check(inside("A standard function to pass later", "comment.line.double-slash.fin"), "line comment");

// strings and chars
check(inside('%d\\n', "string.quoted.double.fin"), "string span broke on escape");
check(has("constant.character.escape.fin", "\\n"), "string escape unmatched");
check(has("string.quoted.single.fin", "' '"), "char literal write_repeat(' ') unmatched");
check(inside('tests/samples/structs.fin"', "string.quoted.double.fin"), "import path not a string");

// keywords/types from the single-source lists
for (const [scope, words] of [
  ["keyword.declaration.fin", ["fun", "let", "import", "struct"]],
  ["keyword.control.fin", ["return", "blame", "in", "foreach", "while"]],
  ["constant.language.fin", ["true", "false", "null"]],
  ["storage.type.fin", ["int", "string"]],
])
  for (const w of words)
    check(has(scope, w), w + " not scoped as " + scope);
// shape pins: directives fire on `#for`, never on preprocessor `#cdef`;
// the macro alternation stays grouped so bare `macro` cannot match it.
{
  const dir = grammar.patterns.find((r) => r.name === "keyword.directive.fin");
  const macro = grammar.patterns.find((r) => r.name === "keyword.other.macro.fin");
  check(new RegExp(dir.match, "y").test("#for x", 0) && !new RegExp(dir.match, "y").test("#cdef", 0),
    "directive rule shape wrong: " + dir.match);
  check(new RegExp(macro.match, "y").test("@define", 0) && !(new RegExp(macro.match, "y").test("macro", 0)),
    "macro rule shape wrong (ungrouped?): " + macro.match);
  check(/^@\(\?:/.test(macro.match), "macro alternation not grouped");
}
// types reachable only inside `<...>` annotations surface through the
// best-effort annotation span instead, and bare `Self` (deeptest2.fin:15)
check(inside("<float>", "meta.type.annotation.fin"), "annotated <float> lost");
check(inside("<bool>", "meta.type.annotation.fin"), "annotated <bool> lost");
check(has("storage.type.fin", "Self"), "Self not scoped as type");

// annotations/generics (best-effort), and no comparison false-claim
check(inside("<Result<Result<int>>", "meta.type.annotation.fin"), "nested generic annotation");
check(inside("<fn(int, int", "meta.type.annotation.fin"), "fn-type annotation");
check(inside("<noret>", "meta.type.annotation.fin"), "return-type annotation");
check(!events.some((e) => e.scope === "meta.type.annotation.fin" && String(e.text).includes("i <= 10")),
  "comparison i <= 10 misread as type annotation");

// grants, attribute block, macros (lib/std/typing.fin)
check(inside("llvm_name", "meta.directive.grant.fin"), "#[...] grant region");
check(events.some((e) => e.scope === "meta.attribute-block.fin" && e.end - e.start > 4), "%{ }% block span");
check(has("keyword.other.macro.fin", "@define"), "@define macro keyword");

// operators and punctuation
for (const op of ["=>", "->", "::", "++", "==", "&&", "<="])
  check(has("keyword.operator.fin", op), "operator " + op + " unmatched");
check(has("punctuation.definition.punctuation.fin", ";"), "punctuation ; unmatched");

// ordering: keywords win over the identifier catch-all
check(!has("variable.other.fin", "foreach"), "keyword leaked into variable.other.fin");

console.log("PASS: JSON valid, all regexes compile, corpus (plus lib/std attrs) captured as asserted");
