// =============================================================================
// Fin Programming Language Compiler
//
// Made with ❤️ for someone special.
//
// This project is genuinely built on love and dedication, a way to express your love can be done through Fin.
// Fin is a semi-object-orientated programming language that specialized in control, giving so much control to the user while asking for the bare minimum.
// Fin is the name of the Angel from BlackNightmare-Stories not yet published.
// “Love is the reason this project exists”
// -----------------------------------------------------------------------------
// Author: M1778
// Repository: https://github.com/M1778/Fin
// Profile: https://github.com/M1778M/
// -----------------------------------------------------------------------------
// Copyright (C) 2027 M1778
//
// This file is part of the Fin Programming Language Compiler.
//
// Fin is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Fin is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Fin.  If not, see <https://www.gnu.org/licenses/>.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================

#include "DiagnosticEngine.hpp"
#include "../utils/Levenshtein.hpp"
#include <sstream>
#include <iterator>
#include <fstream>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fmt/core.h>
#include <fmt/color.h>

#ifdef _WIN32
#include <io.h>
#define FIN_ISATTY _isatty
#define FIN_FILENO _fileno
#else
#include <unistd.h>
#define FIN_ISATTY isatty
#define FIN_FILENO fileno
#endif

namespace fin {

namespace {

// Every byte a diagnostic produces goes to stderr; stdout is reserved.
void put(bool color, const fmt::text_style& style, const std::string& text) {
    if (color) fmt::print(stderr, style, "{}", text);
    else fmt::print(stderr, "{}", text);
}

void putPlain(const std::string& text) {
    fmt::print(stderr, "{}", text);
}

// A JSON string has to be valid UTF-8, and a diagnostic message can carry an
// arbitrary byte out of the source — the lexer's catch-all matches one byte at a
// time, so a multi-byte character arrives split. Any byte that is not part of a
// well-formed UTF-8 sequence becomes U+FFFD, so no diagnostic can produce a line
// the consumer cannot parse.
int utf8SequenceLength(const std::string& in, size_t i) {
    unsigned char c = (unsigned char)in[i];
    int len = 0;
    unsigned int cp = 0;
    if (c < 0x80) return 1;
    else if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1Fu; }
    else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0Fu; }
    else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07u; }
    else return 0;

    if (i + (size_t)len > in.size()) return 0;
    for (int k = 1; k < len; ++k) {
        unsigned char cc = (unsigned char)in[i + k];
        if ((cc & 0xC0) != 0x80) return 0;
        cp = (cp << 6) | (cc & 0x3Fu);
    }
    // Reject overlong encodings, surrogates and out-of-range code points.
    if (len == 2 && cp < 0x80) return 0;
    if (len == 3 && cp < 0x800) return 0;
    if (len == 4 && cp < 0x10000) return 0;
    if (cp > 0x10FFFF) return 0;
    if (cp >= 0xD800 && cp <= 0xDFFF) return 0;
    return len;
}

std::string jsonEscape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (size_t i = 0; i < in.size();) {
        unsigned char c = (unsigned char)in[i];
        if (c < 0x80) {
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                case '\b': out += "\\b";  break;
                case '\f': out += "\\f";  break;
                default:
                    if (c < 0x20) out += fmt::format("\\u{:04x}", (int)c);
                    else out += (char)c;
            }
            ++i;
            continue;
        }
        int len = utf8SequenceLength(in, i);
        if (len == 0) {
            out += "\\ufffd";   // U+FFFD REPLACEMENT CHARACTER
            ++i;
            continue;
        }
        out.append(in, i, (size_t)len);
        i += (size_t)len;
    }
    return out;
}

std::string jsonString(const std::string& s) {
    return "\"" + jsonEscape(s) + "\"";
}

// A key whose value is absent ships as JSON null rather than being omitted, so
// a consumer never has to distinguish "missing key" from "no value".
std::string jsonOptional(const std::string& s) {
    return s.empty() ? std::string("null") : jsonString(s);
}

const char* severityName(DiagnosticSeverity s) {
    switch (s) {
        case DiagnosticSeverity::Error:   return "error";
        case DiagnosticSeverity::Warning: return "warning";
        case DiagnosticSeverity::Note:    return "note";
    }
    return "error";
}

// The stable code registry (issue #47). One row per diagnostic family, matched
// on the message's stable shape rather than its interpolated values: every
// `unsupported()` refusal reads `codegen: <what> is not lowered yet` whatever
// `<what>` names, and every checkType failure starts `Type mismatch:`. Matching
// the shape keeps one code per family even when two call sites describe the same
// gap differently, which is the case message-text matching could never cover.
//
// Codes are never redefined and rows are only appended. The human renderer
// ignores `code` entirely, so this table cannot change a byte of human output.
//
// Numbering is by the pass that owns the rule, not by the order rows were added,
// so the number tells a reader where to look:
//
//   E01xx  backend / code generation (E0101: a construct with no lowering)
//   E02xx  types (E0201: a value whose type does not fit its position)
//   E03xx  names: a name that does not resolve, or a member the type does not
//          have
//   E04xx  declarations: a generic arity, a conformance gap, a field whose type
//          has no layout, an unknown attribute, `Self` with no struct
//   E05xx  operations: an operator or statement the operand types cannot
//          support, and match completeness
//   E06xx  modules and imports: the loader could not find, load, or satisfy an
//          import
//   E07xx  ownership: a reference that would outlive the storage it names
//   E08xx  invocation: a diagnostic about the command line rather than a place
//          in a file (reserved and empty: see docs/diagnostics.md)
//   E09xx  lexer and syntax: source that is not a program
//
// The two published codes keep their numbers, so E0101 and E0201 stay where they
// are and everything else was appended around them.
//
// One row is also the whole user-facing catalogue behind `finc --explain`
// (docs/diagnostics.md). That is the point of the two living together: a code
// cannot be explained without being emitted, because the explanation is a field
// of the rule that assigns it, and a new family cannot be emitted without an
// explanation, because the same edit adds the row. `example` is compiled by
// tests/test_diagnostic_help.cpp, so an example that stops reproducing its code
// fails the suite instead of aging into a lie.
struct CodeRow {
    const char* code;
    const char* prefix;    // the message starts with this, or "" for none
    const char* contains;  // the message also contains this, or "" for none
    const char* severity;  // every call site raises this severity
    const char* summary;   // one line, for `--explain list`
    const char* detail;    // the long form, for `--explain <code>`
    const char* example;   // a minimal program that triggers the code
    const char* owner;     // the ruling or sample that owns the rule
};

constexpr CodeRow kCodeRegistry[] = {
    {
        "E0101", "codegen: ", "is not lowered yet", "error",
        "the backend cannot lower the construct, and says so instead of guessing",
        "The front end accepted the program, but the LLVM backend has no lowering for one\n"
        "construct in it. The message names that construct; the rule behind the refusal is\n"
        "that an unsupported runtime operation is reported, never silently dropped or\n"
        "emitted as guessed IR (AGENTS.md, Backend invariant).\n"
        "\n"
        "Some refusals are permanent rulings rather than gaps (struct '==' is\n"
        "declared-only, ADR 0036), and some are known gaps with an ADR tracking them. A\n"
        "refusal names the construct so the next reader can tell which this is. Checking a\n"
        "program without '-c' or '-o' never reaches lowering, so a clean frontend check\n"
        "does not rule this code out; compile an object to see it.\n",
        "fun make<T>() <T> { return 1; }\n"
        "fun main() <noret> { let x <int> = make(); }\n",
        "AGENTS.md, Backend invariant; each refusal names the gap it belongs to"
    },
    {
        "E0201", "Type mismatch:", "", "error",
        "a value's type is not assignable to the type its position requires",
        "Semantic analysis refused a value because it cannot stand where the program puts\n"
        "it: an initializer against its annotation, an argument against its parameter, an\n"
        "operand against the other operand or the declared result. The message spells the\n"
        "wanted type first and the type found second: expected 'int', got 'string'.\n"
        "\n"
        "Integers widen implicitly and never narrow (ADR 0022), so 'int' into 'long' is\n"
        "accepted while 'long' into 'int' is this code. A negative constant is never an\n"
        "unsigned value, so 'let x <ulong> = -1' is refused even though the width fits.\n"
        "Fin has no cast syntax ruled on yet, so a genuine narrowing has no spelling.\n"
        "One shape carries its own help: 'object' into 'any' is refused with\n"
        "`cast<any>(x)` named, because erasure cannot read a runtime type back.\n",
        "fun main() <noret> { let x <int> = \"s\"; }\n",
        "ADR 0022 (integer widening); samples under tests/samples/ are the corpus"
    },
    // --- E03xx: names that do not resolve ----------------------------------
    {
        "E0301", "Undefined ", "", "error",
        "a name was used that nothing in scope declares",
        "Semantic analysis reached a name and found no declaration for it: a local, a\n"
        "parameter, a type, a struct, or a function. The message says which kind it\n"
        "looked for and quotes the spelling as written, so `Undefined variable 'x'` and\n"
        "`Undefined type 'x'` are one rule with two questions rather than two rules.\n"
        "\n"
        "Resolution is lexical and, for a name used before its declaration in the same\n"
        "block, order-dependent where the corpus proves it must be. An import binds only\n"
        "what it names: a whole-module import does not bring a macro into scope, and a\n"
        "named import carries exactly its names. A typo in a keyword reaches this code\n"
        "only when it names no declaration; the lexer's own typo hint goes in `help`.\n"
        "\n"
        "Fix it by declaring the name, importing it, or correcting the spelling. The\n"
        "`help` key carries a `did you mean` guess when the misspelling is close enough to\n"
        "a keyword to suggest one, which is a hint and never part of the code.\n",
        "fun main() <noret> { let x <int> = nosuch; }\n",
        "docs/guide/04-functions-and-scopes.md; samples under tests/samples/"
    },
    {
        "E0302", "", " has no member '", "error",
        "the named member does not exist on the type the expression was written against",
        "A member access named a field, enum member, or positional member the receiver's\n"
        "type does not have. The message names the receiver's type and the member as\n"
        "written, so the same rule covers `Struct 'S' has no member 'b'`,\n"
        "`Enum 'E' has no member 'C'`, and the positional-member forms.\n"
        "\n"
        "A struct's fields are the ones its body declares, plus any a parent contributes\n"
        "(ADR 0026); an enum's members are the enumerators its body lists (ADR 0041).\n"
        "Members are looked up on the type, not on the value, so a name that exists on a\n"
        "different type with the same field names is still this code.\n"
        "\n"
        "Fix it by writing a member the type declares, or by declaring the member. A\n"
        "member that exists but is not public is a different code (E0304), and a method\n"
        "that does not exist on a type that has methods is E0303.\n",
        "struct S {\n"
        "    a <int>\n"
        "}\n"
        "fun main() <noret> { let s <S> = S { a: 1 }; let y <int> = s.b; }\n",
        "docs/guide/06-structs-and-classes.md; ADR 0026, ADR 0041"
    },
    {
        "E0303", "", " not found in ", "error",
        "a method or static method was called that the receiver's type does not declare",
        "A call was written in method position — `x.m()` or `T::m()` — and the receiver's\n"
        "type declares no method by that name. The message names the method and the type,\n"
        "which is the whole rule: method lookup is on the receiver's type and its\n"
        "ancestry, and a free function of the same name is not a method.\n"
        "\n"
        "This is separate from E0302 because the two questions have different answers. A\n"
        "missing field is a misspelt or undeclared member; a missing method is usually a\n"
        "call that should have been a free function, or a method on another type. The\n"
        "message distinguishes a dot call (`not found in type`) from a `::` call (`not\n"
        "found in`), because a static lookup and a receiver lookup search different\n"
        "tables.\n"
        "\n"
        "Fix it by declaring the method on the type, spelling the method that exists, or\n"
        "calling the free function by name.\n",
        "struct S {\n"
        "    a <int>\n"
        "}\n"
        "fun main() <noret> { let s <S> = S { a: 1 }; let y <int> = s.nope(); }\n",
        "docs/guide/06-structs-and-classes.md; ADR 0027 (interface method dispatch)"
    },
    {
        "E0304", "Cannot access private ", "", "error",
        "a private method or field was used from outside the type that declares it",
        "A `priv:` label (or an unlabelled member under the default visibility) makes a\n"
        "member reachable only from inside its own type's body. This diagnostic is that\n"
        "rule being enforced: the member exists, and the access is from somewhere else.\n"
        "\n"
        "The message says whether it was a method or a field, and names both the member\n"
        "and the struct. `priv:` and `pub:` are visibility labels that set the default for\n"
        "the members after them (CONTEXT.md), so a member is private by position as well\n"
        "as by spelling.\n"
        "\n"
        "Fix it by marking the member `pub`, or by moving the access inside the type. A\n"
        "private field written in a struct literal is E0305 rather than this code, because\n"
        "initialising a field and reading one are different operations.\n",
        "struct S {\n"
        "    priv: fun secret() <int> { return 1; }\n"
        "}\n"
        "fun main() <noret> { let s <S> = S { }; let y <int> = s.secret(); }\n",
        "CONTEXT.md, visibility labels; docs/guide/06-structs-and-classes.md"
    },
    {
        "E0305", "Cannot initialise private ", "", "error",
        "a private field was set from a struct literal written outside its type",
        "Struct-literal initialisation writes fields directly, and a private field cannot\n"
        "be written from outside the type that declares it. This is the write half of the\n"
        "visibility rule that E0304 enforces on reads: the field exists, the caller may\n"
        "not fill it in.\n"
        "\n"
        "The type's own constructors and methods can initialise it, which is what makes a\n"
        "private field meaningful: outside code constructs through the type's own\n"
        "construction rather than by writing the field. Inside the type's body the same\n"
        "literal is accepted.\n"
        "\n"
        "Fix it by marking the field `pub`, by constructing through a constructor the type\n"
        "declares, or by moving the literal inside the type's own scope.\n",
        "struct S {\n"
        "    priv: a <int>\n"
        "}\n"
        "fun main() <noret> { let s <S> = S { a: 1 }; }\n",
        "CONTEXT.md, visibility labels; docs/guide/06-structs-and-classes.md"
    },
    // --- E04xx: declarations that cannot stand -----------------------------
    {
        "E0401", "Generic count mismatch", "", "error",
        "a generic type or function was written with the wrong number of type arguments",
        "A declaration says how many generic parameters it takes; a use wrote a different\n"
        "number in `::<...>`. The message spells the declared count and the written count,\n"
        "because the fix is one of the two numbers and nothing else.\n"
        "\n"
        "The bare `Generic count mismatch` form is the same rule at a site that has no\n"
        "count to print — an instantiation reached through a path that does not carry the\n"
        "written list. Treat it the same way: the arity at the use does not match the\n"
        "declaration.\n"
        "\n"
        "Fix it by writing exactly the declared number of arguments, or by omitting the\n"
        "list where inference can supply it. `Box<int>` for a `Box<T>` is right;\n"
        "`Box<int, string>` is this code.\n",
        "struct Box<T> {\n"
        "    val <T>\n"
        "}\n"
        "fun main() <noret> { let b <Box<int>> = Box::<int, string> { val: 1 }; }\n",
        "docs/guide/09-generics.md"
    },
    {
        "E0402", "", "does not implement", "error",
        "a type claimed a conformance it does not satisfy, or a bound was given a type outside it",
        "Fin has two conformance questions and this code answers both. An `implements`\n"
        "block or a struct's parent list claims an interface, and the type is missing a\n"
        "member the interface requires (ADR 0027). A generic bound `T: I` is satisfied only\n"
        "by a type that implements `I` or by a memberless mock, and a primitive argument to\n"
        "a member-full bound is refused here too.\n"
        "\n"
        "The same predicate decides both, so an `S<int>` instantiation and an assignment\n"
        "into a bound agree instead of each having half the rule. A struct-vs-interface\n"
        "question reads `does not implement interface`; a bound against a non-interface\n"
        "constraint reads `does not implement`.\n"
        "\n"
        "Fix it by implementing the missing members, by declaring the type as implementing\n"
        "the interface, or by passing a type that satisfies the bound.\n",
        "interface I {\n"
        "    pub fun f() <int>;\n"
        "}\n"
        "struct S: <I> { }\n"
        "fun main() <noret> { let s <S> = S { }; }\n",
        "ADR 0027; docs/guide/10-interfaces.md"
    },
    {
        "E0403", "Function '", "is missing a return statement", "error",
        "a function that declares a result type can reach its end without returning one",
        "A function whose declared result is not `noret` or `void` must return a value on\n"
        "every path that reaches the end of its body. This diagnostic names the function\n"
        "and says the gap is on *some* paths, because a body that never falls through is\n"
        "accepted and one that can is not: `if c { return 1; }` alone is this code, while\n"
        "`if c { return 1; } else { return 2; }` is not.\n"
        "\n"
        "Fix it by returning on the missing path, by making the last statement an\n"
        "unconditional `return`, or by declaring the function `noret` if it genuinely\n"
        "never produces a value.\n",
        "fun f() <int> { }\n"
        "fun main() <noret> { let x <int> = f(); }\n",
        "docs/guide/04-functions-and-scopes.md"
    },
    {
        "E0404", "'Self' used outside of struct", "", "error",
        "`Self` was written somewhere that has no enclosing struct to stand for",
        "`Self` is the name a struct's body uses for the type being declared. Outside such\n"
        "a body there is nothing for it to mean, and this diagnostic says so by name. It\n"
        "is an error rather than a guessed type because guessing would bind `Self` to\n"
        "whatever type the call site happened to construct.\n"
        "\n"
        "The same rule covers `Self(...)` as a constructor call: inside a struct body it\n"
        "constructs that struct, and outside one there is no `Self` to construct.\n"
        "\n"
        "Fix it by naming the concrete type, or by moving the code inside the struct whose\n"
        "`Self` it means.\n",
        "fun main() <noret> { let x <int> = Self(); }\n",
        "docs/guide/06-structs-and-classes.md"
    },
    {
        "E0405", "Unknown attribute '", "", "error",
        "an attribute name is not one the compiler implements",
        "Attributes are `#[name]` markers the compiler reads, so a name it does not\n"
        "implement cannot be ignored: an ignored attribute is a program that silently\n"
        "does not get the behaviour it asked for. The message quotes the name.\n"
        "\n"
        "The known set is the compiler's own. `pub:`/`priv:` are visibility labels rather\n"
        "than attributes; the declarative attributes the compiler acts on include\n"
        "`global`, `class`, `export`, `overwrite`, `implements`, `provides`, `protocol`,\n"
        "`slaveof`, `on`, `use`, `attribute`, `type`, `RT`, `future` and the `std`-scoped\n"
        "spellings. A compile-time library cannot register one yet: wave 4 is where\n"
        "`compiler.grammar.declare` gives a library an attribute of its own\n"
        "(docs/compiler-api.md, Q2).\n"
        "\n"
        "Fix it by spelling an attribute that exists. The authoritative list is\n"
        "`isReaderOwnedAttribute` in src/semantics/impl/Analyzer_CompilerApi.cpp, together\n"
        "with the parser's attribute rules.\n",
        "#[nosuchattr]\n"
        "fun g() <noret> { }\n"
        "fun main() <noret> { }\n",
        "src/semantics/impl/Analyzer_CompilerApi.cpp"
    },
    {
        "E0406", "", "has union type '", "error",
        "a struct field's type is a union, which has no single layout to store",
        "A union type `A | B | C` describes a value that may be any one of its members,\n"
        "and which one is known only at run time. A struct field needs a fixed layout, so\n"
        "a union-typed field is refused at declaration time rather than given the layout\n"
        "of its first member (ADR 0015, docs/compiler-api.md Q12).\n"
        "\n"
        "The message names the field and the union alias. This is not a missing lowering:\n"
        "the pointer map of a union is a diagnostic by ruling, and a silent zero map would\n"
        "be a wrong answer instead of a refusal.\n"
        "\n"
        "Fix it by storing a concrete member type, by using an enum with a payload (ADR\n"
        "0041), or by using an erasure alias such as `Offer` where a dynamic choice is\n"
        "genuinely wanted.\n",
        "type Number = int | uint | float;\n"
        "struct S { pub v <Number>, }\n"
        "fun main() <noret> { }\n",
        "ADR 0015; docs/compiler-api.md Q12; tests/samples/union_pointer_map.fin"
    },
    // --- E05xx: operations the operands cannot support ---------------------
    {
        "E0501", "Cannot ", " immutable variable '", "error",
        "a `const` binding was written after its declaration",
        "`const` declares a binding that is never written again, and `let` declares one\n"
        "that may be. This diagnostic is a write to the former, either through assignment\n"
        "(`x = 2`) or through an increment or decrement (`x++`), which are writes with\n"
        "operator spelling.\n"
        "\n"
        "The message names the variable and the verb, so the two spellings are one rule:\n"
        "a `const` is immutable for the whole of its lifetime, not only outside the scope\n"
        "that declared it.\n"
        "\n"
        "Fix it by declaring the binding `let`, or by removing the write. A `readonly`\n"
        "field is a different restriction (E0502) because it is about a field of a value\n"
        "rather than a binding's name.\n",
        "fun main() <noret> { const x <int> = 1; x = 2; }\n",
        "docs/guide/02-variables-and-types.md"
    },
    {
        "E0502", "Cannot assign to readonly field '", "", "error",
        "a `readonly` field was assigned outside the type that declares it",
        "`readonly` is a static property of a field: the declaring type's own methods may\n"
        "initialise or update it, and every other write is refused before code generation.\n"
        "That is what makes it a rule rather than a runtime check — a try/catch cannot\n"
        "turn an avoidable violation into a valid program.\n"
        "\n"
        "The message names the field and the struct. The rule is scoped to the declaring\n"
        "type, not to the file: the same assignment inside the type's body is accepted.\n"
        "ADR 0045's `restrict` is the value-level snapshot of the same idea and is a\n"
        "different mechanism.\n"
        "\n"
        "Fix it by removing the assignment, by marking the field writable, or by moving\n"
        "the write into the type's own methods.\n",
        "struct S {\n"
        "    readonly a <int>\n"
        "}\n"
        "fun main() <noret> { let s <S> = S { a: 1 }; s.a = 2; }\n",
        "CONTEXT.md, readonly member; ADR 0045"
    },
    {
        "E0503", "Cannot delete non-pointer type ", "", "error",
        "a value was `delete`d that names no storage the program allocated",
        "`delete` frees storage a pointer names, so its operand has to be a pointer — or a\n"
        "dynamic array `[T]`, which is what `new [T, n]` produces and which\n"
        "`delete self._arr` frees in `lib/std/collection.fin`. A value of any other type\n"
        "names storage `delete` cannot free, and a *fixed*-extent array `[T, n]` is\n"
        "refused because an annotation and an array literal both produce one and neither\n"
        "came from an allocator.\n"
        "\n"
        "The message quotes the operand's type. One thing a type cannot say is\n"
        "provenance, so a `[T]` that decayed from a literal is accepted here; that is the\n"
        "same latitude `delete p` has over a pointer to a local.\n"
        "\n"
        "Fix it by deleting the pointer or dynamic array that owns the allocation, or by\n"
        "removing the `delete` and letting scope exit release the value (ADR 0030).\n",
        "fun main() <noret> { let x <int> = 1; delete x; }\n",
        "ADR 0003 (memory management is a library); ADR 0030"
    },
    {
        "E0504", "An index must be an integer", "", "error",
        "a subscript was written with a non-integer index",
        "Arrays and pointers are addressed by an integer position, so a subscript written\n"
        "with a string, a struct, or any other type has no meaning to compute. The message\n"
        "quotes the index's type.\n"
        "\n"
        "The rule is about the index's type rather than its value: a negative integer\n"
        "reaches E0505 and an integer past a known extent reaches E0506, and both are\n"
        "integer-index questions rather than this one.\n"
        "\n"
        "Fix it by computing an integer position, or by indexing the collection the way\n"
        "its own API supports (`get`, `try_get`).\n",
        "fun main() <noret> { let a <[int]> = [1]; let x <int> = a[\"s\"]; }\n",
        "ADR 0025 (a dynamic array is a pointer and a length)"
    },
    {
        "E0505", "An array index cannot be negative", "", "error",
        "a constant array index is negative",
        "An array is addressed from zero, so no length makes `-1` a valid position. This is\n"
        "refused whether or not the array has a known extent, because the rule is about\n"
        "the sign and not about the bound.\n"
        "\n"
        "Only a constant index can be decided here; a run-time index is not this code,\n"
        "because the compiler does not know its value. A constant index that is\n"
        "non-negative but past a known extent is E0506, which quotes the extent.\n"
        "\n"
        "Fix it by writing a non-negative index, or by guarding the position before the\n"
        "subscript when it comes from a computation.\n",
        "fun main() <noret> { let a <[int, 3]> = [1, 2, 3]; let x <int> = a[-1]; }\n",
        "ADR 0025; tests/samples/arrays.fin"
    },
    {
        "E0506", "", "is out of bounds for an array", "error",
        "a constant array index is past the array's known extent",
        "An array type written `[int, 3]` carries its length, so the compiler can decide a\n"
        "constant subscript against it. This diagnostic quotes the index and the extent,\n"
        "because `a[5]` is only wrong relative to a length written somewhere else. An index\n"
        "too large to read as a number is reported as `that index` rather than a number no\n"
        "reader could finish reading.\n"
        "\n"
        "A run-time index is not this code: the value is unknown, so no bound can be\n"
        "checked. A negative constant is E0505 and is decided before the extent is\n"
        "consulted.\n"
        "\n"
        "Fix it by writing an index inside the extent, or by widening the array's declared\n"
        "length.\n",
        "fun main() <noret> { let a <[int, 3]> = [1, 2, 3]; let x <int> = a[5]; }\n",
        "ADR 0025; tests/samples/arrays.fin"
    },
    {
        "E0507", "Arithmetic operator requires numeric operands", "", "error",
        "an arithmetic operator was given operands that are not numbers",
        "`+`, `-`, `*`, `/` and the rest compute over numbers. The message names both\n"
        "operand types, because the same operator over two non-numeric types is the same\n"
        "mistake whichever pair it is.\n"
        "\n"
        "String concatenation is not `+` in Fin: string equality compares content (ADR\n"
        "0049), and building a string from parts is the formatting API's job. A struct's\n"
        "operator is its own declared operator and is reached by name, so a user-defined\n"
        "`+` on a struct is not this code.\n"
        "\n"
        "Fix it by giving the operator numeric operands, or by calling the operation the\n"
        "types actually define.\n",
        "fun main() <noret> { let s <string> = \"a\"; let t <string> = \"b\"; let x <int> = s + t; }\n",
        "ADR 0049; docs/guide/02-variables-and-types.md"
    },
    {
        "E0508", "Shift count ", "out of range", "error",
        "a shift was written with a count outside the shifted type's width",
        "A shift of an N-bit value by N or more has no defined result in the type it is\n"
        "written at, and a negative count has none at all. This diagnostic quotes the\n"
        "count, the width, and the type, which are the three numbers the fix needs.\n"
        "\n"
        "The check is on a constant count, because a run-time count's value is unknown.\n"
        "The width is the shifted operand's own width, so `1 << 40` on a 32-bit `int` is\n"
        "this code and the same count on a `long` is not.\n"
        "\n"
        "Fix it by writing a count in `[0, width)`, or by widening the operand.\n",
        "fun main() <noret> { let x <int> = 1 << 40; }\n",
        "ADR 0054 (signed integer overflow wraps); docs/guide/02-variables-and-types.md"
    },
    {
        "E0509", "Invalid cast from ", "", "error",
        "an explicit conversion was written between two types that have none",
        "`cast<T>(x)` is the programmer overriding the checker, and it is not\n"
        "unchecked: the pair has to be one the language rules a conversion for. The\n"
        "message names the source and target types, which is the whole question.\n"
        "\n"
        "The admitted pairs are: a type with itself; any two numeric primitives (so a\n"
        "cast does narrow, unlike an assignment, which only widens per ADR 0022); a bool\n"
        "and an integer; an integer and a pointer, in that direction; two pointers; a\n"
        "type and a generic parameter; anything touching `any` in either direction, which\n"
        "is how a value enters and leaves an erased slot; an enum and a non-string\n"
        "primitive, in either direction, because an enum is an integer at the\n"
        "representation level; and a value into the nullable of a type it could cast or\n"
        "assign to.\n"
        "\n"
        "What is refused is a `string` against anything that is not a string (formatting\n"
        "is not a cast, #93), a struct that is not an enum, and a fixed-extent array into\n"
        "an enum (`casts` from `arrays_enums.fin:23`'s commented-out line). `cast<auto>`\n"
        "is accepted and means the operand's own type.\n"
        "\n"
        "Fix it by casting through a type that has a ruled conversion, or by converting\n"
        "with the API that owns the operation.\n",
        "fun main() <noret> { let s <string> = \"s\"; let y <int> = cast<int>(s); }\n",
        "ADR 0022 (widening applies to assignment, not to an explicit cast); #93; ADR 0034",
    },
    {
        "E0510", "", "used outside of loop", "error",
        "`break` or `continue` was written where there is no loop to leave",
        "`break` leaves the nearest enclosing loop and `continue` starts its next\n"
        "iteration. Outside a loop there is nothing either could do, so both are refused\n"
        "rather than silently ignored. The message quotes the keyword written.\n"
        "\n"
        "The check is lexical: the keyword must appear inside a loop body, decided by the\n"
        "walk's own loop nesting rather than by whether a loop happens to call the code at\n"
        "run time. A lambda written outside any loop is this code; one written lexically\n"
        "inside a loop is a known gap in the check and is refused later by the backend\n"
        "rather than here.\n"
        "\n"
        "Fix it by moving the statement into the loop body, or by removing it.\n",
        "fun main() <noret> { break; }\n",
        "docs/guide/05-control-flow.md"
    },
    {
        "E0511", "non-exhaustive match", "", "error",
        "a `match` expression does not cover every value the scrutinee can take",
        "A `match` is an expression, so it must produce a value for every possible\n"
        "scrutinee. This diagnostic lists what is not covered: unmatched enum members, the\n"
        "constants a literal pattern cannot cover, or the surviving values after the arms\n"
        "written (ADR 0050).\n"
        "\n"
        "An enum match covers its members or needs `_`; a nullable match covers `null` and\n"
        "the present value; a constant match over an integer or string always needs `_`,\n"
        "because literals never cover every value. The message says which of these applies\n"
        "by naming what is missing.\n"
        "\n"
        "Fix it by adding the missing arms, or by adding `_` where a catch-all is genuinely\n"
        "what the program means.\n",
        "enum E { A, B }\n"
        "fun main() <noret> { let e <E> = A; let n <int> = match (e) { A => 1 }; }\n",
        "ADR 0050; tests/samples/match.fin"
    },
    {
        "E0512", "unreachable match arm", "", "error",
        "a match arm can never be selected because an earlier arm already covers it",
        "Arms are tried in order, so an arm after a `_`, after a binder that covers every\n"
        "present value, or a repeated member can never run. A never-selected arm is dead\n"
        "code the author did not know was dead, so it is an error rather than a warning.\n"
        "\n"
        "The message says which earlier shape covers it — `_`, a binder, or a repeated\n"
        "member — so the fix is to move the arm earlier or delete it. A duplicate is\n"
        "E0513 when the repeated member is exactly the earlier one and unreachable when an\n"
        "earlier catch-all is what covers it.\n"
        "\n"
        "Fix it by deleting the arm, or by moving it before the arm that covers it.\n",
        "enum E { A, B }\n"
        "fun main() <noret> { let e <E> = A; let n <int> = match (e) { _ => 1, A => 2 }; }\n",
        "ADR 0050; tests/samples/match.fin"
    },
    {
        "E0513", "duplicate match arm", "", "error",
        "two match arms name the same member, constant, or `null`",
        "Two arms that name the same value make the second dead: the first already\n"
        "selected. This is separate from E0512 because the cover is the same value rather\n"
        "than a catch-all, and the fix is to remove one of the two rather than to move it.\n"
        "\n"
        "The message names the repeated member, constant, or `null`. The rule applies per\n"
        "kind of pattern — enum members, literal constants, and the nullable `null` arm —\n"
        "and the message says which.\n"
        "\n"
        "Fix it by deleting one of the two arms, or by writing the second's body into the\n"
        "first.\n",
        "enum E { A, B }\n"
        "fun main() <noret> { let e <E> = A; let n <int> = match (e) { A => 1, A => 2, _ => 3 }; }\n",
        "ADR 0050; tests/samples/match.fin"
    },
    {
        "E0514", "Cannot dereference non-pointer type ", "", "error",
        "the `*` prefix was applied to a value that is not a pointer",
        "`*p` reads the value a pointer names, so its operand must be a pointer. The\n"
        "message quotes the operand's type. An `&T` and a `[T]`-style pointer both\n"
        "dereference; a primitive, struct, or interface value is already the value and has\n"
        "nothing to follow.\n"
        "\n"
        "Fix it by dereferencing the pointer that holds the storage, or by removing the\n"
        "`*` if the value was the thing wanted.\n",
        "fun main() <noret> { let x <int> = 1; let y <int> = *x; }\n",
        "ADR 0025; docs/guide/02-variables-and-types.md"
    },
    // --- E06xx: modules and imports ----------------------------------------
    {
        "E0601", "module not found: ", "", "error",
        "an import named a module no search path holds",
        "The loader searched its paths and found no file for the import, so nothing was\n"
        "compiled from it. This diagnostic is about the search, not about the file's\n"
        "contents: the `help` key lists the paths searched, in order, so the reader can\n"
        "see which one was expected to hold the module.\n"
        "\n"
        "A quoted import is a path looked up beside its importer and then through the\n"
        "search paths; a package import is a dotted name turned into a path and looked up\n"
        "through the search paths only. The message quotes the import as written, so the\n"
        "two searches stay distinguishable.\n"
        "\n"
        "Fix it by adding the module's directory with `-I` or `--fin-libs`, or by\n"
        "correcting the import's spelling. This diagnostic has no source location, because\n"
        "it is about the invocation's search paths as much as about the import line.\n",
        "import { n } from \"no-such-module\";\n"
        "fun main() <noret> { }\n",
        "docs/finc-interface-contract.md, Library search paths; ADR 0009"
    },
    {
        "E0602", "Module '", "does not export", "error",
        "an import named a symbol the module exists but does not provide",
        "The module was found and loaded, and its scope holds no declaration by the name\n"
        "the import asked for. The message names the module and the missing symbol, which\n"
        "is the pair the reader needs: which file to look in, and what to look for.\n"
        "\n"
        "A named import carries exactly the names it names, and a whole-module import\n"
        "brings every name the module declares but still resolves each use. The\n"
        "distinction that matters here is that the module *is* present — a module that is\n"
        "absent is E0601, and a namespace tail the module never declared is E0603.\n"
        "\n"
        "Fix it by spelling an exported name, or by adding the export to the module.\n",
        "import { nope } from error::std;\n"
        "fun main() <noret> { }\n",
        "docs/guide/11-macros-and-preprocessor.md; ADR 0033"
    },
    {
        "E0603", "Module '", "has no namespace", "error",
        "an import named a namespace inside a module that does not declare it",
        "Strict namespaces (ADR 0029-era import rule, issue #90): a `::` tail names a\n"
        "namespace inside the module, and every segment written must be one the module\n"
        "declares. Naming one it does not declare is an error rather than a silent no-op,\n"
        "because a silent no-op binds nothing and the failure surfaces later as an\n"
        "undefined name.\n"
        "\n"
        "The message names the module and the segment that failed, so a multi-segment path\n"
        "that fails deep says exactly which segment was wrong. A quoted file import\n"
        "carries no tail and cannot reach this code.\n"
        "\n"
        "Fix it by writing a namespace the module declares, or by dropping the tail.\n",
        "import { x } from error::nosuch;\n"
        "fun main() <noret> { }\n",
        "docs/guide/11-macros-and-preprocessor.md; issue #90"
    },
    {
        "E0604", "Failed to load module ", "", "error",
        "an import could not be satisfied, and this is the located half of the reason",
        "The loader's own diagnostic (E0601) says why a search failed and has no source\n"
        "location. This code is the import statement's own diagnostic: it points at the\n"
        "line that asked, names the module as written, and is emitted so every import\n"
        "failure has a place in the file beside the invocation-level report.\n"
        "\n"
        "Both are emitted for one failure, which is why a run that cannot find a module\n"
        "shows two codes: E0601 for the search and this one for the site. Neither is a\n"
        "duplicate of the other — one is about paths and one is about the statement.\n"
        "\n"
        "Fix it as for E0601: correct the search paths or the import.\n",
        "import { n } from \"no-such-module\";\n"
        "fun main() <noret> { }\n",
        "docs/finc-interface-contract.md, Library search paths; ADR 0009"
    },
    // --- E07xx: ownership --------------------------------------------------
    {
        "E0701", "", " local variable '", "error",
        "a frame-local's address would be used after the frame that owns it ends",
        "A local variable's storage belongs to the call that declared it. Returning its\n"
        "address, storing it somewhere that outlives the call, or handing it out as an\n"
        "interface would leave a reference to storage no longer owned — a dangling pointer\n"
        "that reads whatever the frame is reused for next (ADR 0055, ADR 0056).\n"
        "\n"
        "The message quotes the variable and distinguishes the three routes: `cannot\n"
        "return the address of`, `cannot store the address of ... into storage that\n"
        "outlives the call`, and `cannot return local variable ... as an interface`. The\n"
        "`help` key names the fix for a return: return caller-owned storage, or an owned\n"
        "heap allocation via `new`.\n"
        "\n"
        "The rule is about storage, not about variables: a reference to a caller's value, a\n"
        "static, or a heap allocation is fine, and only frame-local storage is refused.\n",
        "fun f() <&int> { let d <int> = 1; return &d; }\n"
        "fun main() <noret> { }\n",
        "ADR 0055; ADR 0056; tests/samples/pointer_escape.fin"
    },
    // --- E09xx: source that is not a program -------------------------------
    {
        "E0901", "unrecognised byte in source: ", "", "error",
        "the lexer met a byte that begins no token",
        "The lexer's catch-all rule matched one byte and produced no token for it. The\n"
        "message renders the byte as an escape (`\\x01`) rather than embedding it, because\n"
        "the byte may not be valid UTF-8: the catch-all matches one byte at a time, so a\n"
        "multi-byte character arrives here split, and pasting it into a message would\n"
        "produce a diagnostic the JSON consumer cannot parse.\n"
        "\n"
        "This is an error and not a skip. A byte the lexer silently discarded would leave\n"
        "the surrounding text to parse as whatever it happened to spell, so the failure\n"
        "would surface later as an unrelated syntax error pointing at the wrong line.\n"
        "\n"
        "Fix it by removing the byte, or by writing it inside a string literal or comment\n"
        "where it is text rather than source.\n",
        "fun main() <noret> { let x <int> = 1; \\ }\n",
        "src/diagnostics/DiagnosticEngine.cpp, reportLexerError"
    },
    {
        "E0902", "expected ';'", "", "error",
        "a statement or declaration ended without the semicolon that ends it",
        "Fin ends statements with `;`, so a line that does not end in one is refused where\n"
        "the next token proved the statement was over. The parser points at the end of the\n"
        "broken line rather than at the token that surprised it, because the missing\n"
        "semicolon is the thing to add.\n"
        "\n"
        "This is reported separately from the general `syntax error, unexpected ...` form\n"
        "because the fix is known: when `;` is among the tokens that would have been\n"
        "accepted, naming it is more useful than naming every token that could follow.\n"
        "\n"
        "Fix it by adding the `;`. A missing `;` on the last statement of a block is this\n"
        "code too, and is pointed at the end of that statement's line.\n",
        "fun main() <noret> { let x <int> = 1 }\n",
        "src/parser/parser.y, report_syntax_error (issue #44)"
    },
};

bool startsWith(const std::string& s, const char* prefix) {
    const std::string p(prefix);
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

DiagnosticEngine* g_lexerDiagnostics = nullptr;

// One line out of an explicit line table, rather than this engine's own: the
// cross-file render below walks another file's lines with the same code.
std::string lineIn(const std::vector<std::string>& srcLines, int lineNum) {
    if (lineNum > 0 && lineNum <= (int)srcLines.size()) {
        return srcLines[(size_t)lineNum - 1];
    }
    return "";
}

} // namespace

std::string DiagnosticEngine::codeForMessage(const std::string& message) {
    for (const auto& row : kCodeRegistry) {
        if (row.prefix[0] != '\0' && !startsWith(message, row.prefix)) continue;
        if (row.contains[0] != '\0' && message.find(row.contains) == std::string::npos) continue;
        return row.code;
    }
    return "";
}

static std::vector<DiagnosticDoc> catalogueFromRegistry() {
    std::vector<DiagnosticDoc> out;
    out.reserve(std::size(kCodeRegistry));
    for (const auto& row : kCodeRegistry) {
        // The shape is the literal message family, built from the two fields the
        // emitter actually matches on. `<what>` marks the interpolated part,
        // which is the whole reason the registry matches a shape and not a text.
        // The join is normalised to one space on each side of `<what>`, because
        // a `contains` may begin with the space that separates it from the value
        // it follows (` local variable '`) and the raw concatenation read as two.
        std::string prefix = row.prefix;
        while (!prefix.empty() && prefix.back() == ' ') prefix.pop_back();
        std::string contains = row.contains;
        while (!contains.empty() && contains.front() == ' ') contains.erase(0, 1);
        std::string shape = prefix + "<what>";
        if (!contains.empty()) shape += " " + contains;
        out.push_back({row.code, row.severity, row.summary, row.detail, row.example,
                       row.owner, shape});
    }
    return out;
}

const std::vector<DiagnosticDoc>& diagnosticCatalogue() {
    static const std::vector<DiagnosticDoc> docs = catalogueFromRegistry();
    return docs;
}

const std::vector<DiagnosticRule>& diagnosticRules() {
    static const std::vector<DiagnosticRule> rules = [] {
        std::vector<DiagnosticRule> out;
        out.reserve(std::size(kCodeRegistry));
        for (const auto& row : kCodeRegistry) {
            out.push_back({row.code, row.prefix, row.contains});
        }
        return out;
    }();
    return rules;
}

const DiagnosticDoc* findDiagnosticDoc(const std::string& code) {
    for (const auto& doc : diagnosticCatalogue()) {
        if (doc.code == code) return &doc;
    }
    return nullptr;
}

std::string diagnosticDocText(const DiagnosticDoc& doc) {
    // Paragraphs, one blank line apart. `detail` is written with its own
    // newlines, so the trailing one is trimmed rather than assumed absent.
    std::string detail = doc.detail;
    while (!detail.empty() && detail.back() == '\n') detail.pop_back();

    std::string out = fmt::format("{} ({}): {}\n\n", doc.code, doc.severity, doc.summary);
    if (!doc.shape.empty()) out += fmt::format("Message: {}\n\n", doc.shape);
    out += detail + "\n";
    if (!doc.example.empty()) {
        out += "\nExample:\n\n" + doc.example + "\n";
    }
    if (!doc.owner.empty()) {
        out += "\nRule: " + doc.owner + "\n";
    }
    return out;
}

std::string diagnosticCatalogueText() {
    // Sorted (the table is required to be), one line per code so a reader can
    // scan it and a consumer can split it. The meaning is last so the columns
    // before it stay aligned without a width computed at runtime.
    std::string out;
    for (const auto& doc : diagnosticCatalogue()) {
        out += fmt::format("{:<6} {:<8} {}\n", doc.code, doc.severity, doc.summary);
    }
    return out;
}

void setLexerDiagnostics(DiagnosticEngine* engine) { g_lexerDiagnostics = engine; }
DiagnosticEngine* getLexerDiagnostics() { return g_lexerDiagnostics; }

void reportLexerError(const fin::location& loc, const char* text) {
    std::string what = text ? text : "";
    // The catch-all rule matches exactly one byte, so a non-null `text` that reads as the
    // empty string held the one byte a `const char*` cannot carry: a NUL. Naming it as ''
    // hid the byte in the very message that rejected it.
    if (text && what.empty()) what = std::string(1, '\0');
    // The byte is rendered, not embedded: the catch-all rule matches one byte at
    // a time, so a multi-byte character arrives here split and pasting it into a
    // message produces a diagnostic that is not valid UTF-8.
    std::string shown;
    for (unsigned char c : what) {
        if (c >= 0x20 && c < 0x7F) shown += (char)c;
        else shown += fmt::format("\\x{:02x}", (int)c);
    }
    std::string msg = fmt::format("unrecognised byte in source: '{}'", shown);
    if (g_lexerDiagnostics) {
        g_lexerDiagnostics->reportError(loc, msg);
    } else {
        // No engine installed. Still stderr, still one line, so the byte is
        // never silently dropped the way lexer.l used to drop it.
        fmt::print(stderr, "error: {}\n", msg);
    }
}

// Colour precedence, most specific first:
//   1. an explicit --color=always|never on the command line
//   2. NO_COLOR in the environment, whatever its value including empty
//   3. isatty(stderr)
// Only 2 and 3 are decided here; 1 is decided by setColorMode. A machine
// consumer never passes --color=always, so the flag winning cannot surprise one,
// and a human whose profile sets NO_COLOR can still ask for colour once.
bool DiagnosticEngine::colorAvailable() {
    if (std::getenv("NO_COLOR") != nullptr) return false;
    return FIN_ISATTY(FIN_FILENO(stderr)) != 0;
}

DiagnosticEngine::DiagnosticEngine(std::string source, std::string fname)
    : sourceCode(std::move(source)), filename(std::move(fname)) {
    splitLines();
    color = colorAvailable();

    keywords = {
        "fun", "struct", "enum", "let", "const", "bez", "beton",
        "if", "else", "elseif", "while", "for", "foreach", "return", "break",
        "continue", "import", "sizeof", "typeof", "new", "delete",
        "cast", "interface", "pub", "priv", "static", "macro", "operator",
        "from", "as", "true", "false", "null", "self", "super"
    };

    types = {
        "int", "float", "char", "void", "bool", "string", "noret", "auto", "Self",
        "long", "double", "short", "uint", "ulong", "ushort"
    };
}

void DiagnosticEngine::setColorMode(ColorMode m) {
    colorMode = m;
    switch (m) {
        case ColorMode::Always: color = true; break;
        case ColorMode::Never:  color = false; break;
        case ColorMode::Auto:   color = colorAvailable(); break;
    }
}

void DiagnosticEngine::setSource(std::string src, std::string fname) {
    sourceCode = std::move(src);
    filename = std::move(fname);
    lines.clear();
    splitLines();
}

void DiagnosticEngine::splitLines() {
    lines = splitSourceLines(sourceCode);
}

std::vector<std::string> DiagnosticEngine::splitSourceLines(const std::string& text) {
    std::vector<std::string> out;
    std::stringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        // Tolerate a stray CR so a column never counts one.
        if (!line.empty() && line.back() == '\r') line.pop_back();
        sanitiseForDisplay(line);
        out.push_back(line);
    }
    return out;
}

// Which file and lines a located diagnostic renders against. Empty, self, or
// unreadable means this engine's own -- the old behaviour, kept as the
// fallback rather than a second failure on top of the first.
DiagnosticEngine::RenderSource DiagnosticEngine::renderSourceFor(const std::string& originFile) {
    if (originFile.empty() || originFile == filename) return {filename, &lines};
    auto it = overlays.find(originFile);
    if (it != overlays.end()) return {originFile, &it->second};
    std::ifstream t(originFile, std::ios::binary);
    if (!t.is_open()) return {filename, &lines};
    std::stringstream buffer;
    buffer << t.rdbuf();
    if (t.bad()) return {filename, &lines};
    return {originFile, &overlays.emplace(originFile, splitSourceLines(buffer.str())).first->second};
}

// A rendered snippet is the source line itself, so every byte of the source is a byte the
// compiler writes to a terminal -- and a control byte is not inert there. An interior CR
// returns the cursor to column 0, so whatever the source wrote after it overwrites the
// diagnostic that was just printed; an ESC begins a sequence the terminal executes. Both
// let a .fin file decide what its own diagnostics appear to say. Doing this here rather
// than at each print site covers `help` text too, which quotes words lifted out of these
// same lines.
//
// One byte in, one byte out: the caret is placed by counting bytes along the line
// (`printContext`), so escaping a byte into `\x1b` would point the caret at the wrong
// column. Rendering the byte properly *and* keeping the caret on it means teaching the
// caret about display width, which tabs already need and neither has yet.
//
// Bytes at or above 0x80 are left alone: they are UTF-8 continuation bytes in string
// literals and comments, and replacing them would corrupt text that is perfectly valid.
void DiagnosticEngine::sanitiseForDisplay(std::string& line) {
    for (char& c : line) {
        const unsigned char b = (unsigned char)c;
        if (b == '\t') continue;               // real indentation; replacing it mangles code
        if (b < 0x20 || b == 0x7F) c = '?';
    }
}

std::string DiagnosticEngine::getLine(int lineNum) {
    return lineIn(lines, lineNum);
}

std::string DiagnosticEngine::lineText(int lineNum) const {
    if (lineNum > 0 && lineNum <= (int)lines.size()) {
        return lines[(size_t)lineNum - 1];
    }
    return "";
}

std::string DiagnosticEngine::extractTokenText(const fin::location& loc,
                                               const std::vector<std::string>& srcLines) {
    std::string line = lineIn(srcLines, loc.begin.line);
    if (line.empty()) return "";
    int start = loc.begin.column - 1;
    int end = loc.end.column - 1;
    if (start >= 0 && end <= (int)line.length() && start < end) {
        return line.substr(start, end - start);
    }
    return "";
}

// The two scans below walk *backwards* from a location's first column, which is only
// meaningful if that column is inside the line the engine holds. Both indexed
// `line[cursor]` with `cursor = column - 2` and no upper bound, so a location whose
// column runs past the end of its line read out of bounds -- `std::string::operator[]`
// asserts on this toolchain, and finc died with
// `Assertion '__pos <= size()' failed` and exit 134.
//
// That is reachable from a .fin file today. A diagnostic about an expansion of a macro
// declared in another module carries the *module's* location and is rendered by the
// *root file's* engine, so the column belongs to one file and the line to another; when
// the root line is the shorter of the two, the scan starts past its end. Measured
// (`import { c } from m; ... c!(1)` where `m`'s body names an undeclared `zz`): abort at
// column 35 against a 23-character line, and the same text with twenty characters of
// padding on the caller's line 1 renders instead. It is not new and not about macros --
// the qualified `m.c!(1)` route does it at `f4952f1^`, before any of ADR 0023 -- and it
// is not one of the four codes ADR 0009 gives finc, so a caller reading the status
// learned neither that the build succeeded nor that it was rejected.
//
// The guard is "does this column point into this line at all", not a clamp to the line
// end. Scanning back from the end of an unrelated line would find a word and hand it to
// `checkTypo`, so a wrong location would produce a confident `did you mean` about text
// the location has nothing to do with. There is no previous word here, and saying so is
// the honest answer: the hint is a heuristic and declining to guess costs nothing.
//
// `column - 1` is the 0-based index of the location's first character and equals
// `size()` for a location that begins exactly at end-of-line, so every location that
// does fit is untouched.
//
// What this does NOT do is pick the file: the line above still comes from
// whichever line table the caller hands in. A diagnostic about an expansion
// of an imported macro carries the module's coordinates, so the analyzer
// routes those through `reportErrorFromFile` with the macro's defining file
// (stamped at expansion), and the table is the module's own lines --
// `Soundness_Diagnostics.ADiagnosticFromAnImportedMacroBodyNamesTheModule`.
// Anything else still renders against this engine's file, and the guard above
// is what keeps such a cross-file column from indexing out of bounds.
std::string DiagnosticEngine::getPreviousWord(const fin::location& loc,
                                               const std::vector<std::string>& srcLines) {
    std::string line = lineIn(srcLines, loc.begin.line);
    if (line.empty()) return "";
    if (loc.begin.column - 1 > (int)line.size()) return "";

    int cursor = loc.begin.column - 2;
    while (cursor >= 0 && std::isspace((unsigned char)line[cursor])) cursor--;
    if (cursor < 0) return "";

    int end = cursor + 1;
    while (cursor >= 0 && (std::isalnum((unsigned char)line[cursor]) || line[cursor] == '_')) cursor--;

    return line.substr(cursor + 1, end - (cursor + 1));
}

// The same guard, for the same reason. This is the one with a caller
// (`reportError`), so this is the copy the abort came through; `getPreviousWord` has
// none today and is fixed rather than deleted because the bug in it is identical and
// whoever calls it next would inherit it.
fin::location DiagnosticEngine::getPreviousWordLoc(const fin::location& loc,
                                                     const std::vector<std::string>& srcLines) {
    std::string line = lineIn(srcLines, loc.begin.line);
    if (line.empty()) return loc;
    if (loc.begin.column - 1 > (int)line.size()) return loc;

    int cursor = loc.begin.column - 2;
    while (cursor >= 0 && std::isspace((unsigned char)line[cursor])) cursor--;
    if (cursor < 0) return loc;

    int endCol = cursor + 1;
    while (cursor >= 0 && (std::isalnum((unsigned char)line[cursor]) || line[cursor] == '_')) cursor--;
    int startCol = cursor + 1;

    fin::location newLoc = loc;
    newLoc.begin.column = startCol + 1;
    newLoc.end.column = endCol + 1;
    return newLoc;
}

std::string DiagnosticEngine::checkTypo(const std::string& word) {
    if (word.empty()) return "";
    std::string bestMatch;
    int bestDist = 100;

    for (const auto& kw : keywords) {
        int dist = utils::levenshtein_distance(word, kw);
        if (dist < bestDist) {
            bestDist = dist;
            bestMatch = kw;
        }
    }

    int threshold = (word.length() < 4) ? 1 : 2;
    if (bestDist <= threshold && bestDist < (int)word.length()) {
        return bestMatch;
    }
    return "";
}

void DiagnosticEngine::printHighlightedLine(const std::string& line) {
    std::string word;
    for (size_t i = 0; i < line.length(); ++i) {
        char c = line[i];
        if (std::isalnum((unsigned char)c) || c == '_') {
            word += c;
        } else {
            if (!word.empty()) {
                bool isKw = false;
                for (const auto& k : keywords) if (k == word) isKw = true;
                bool isType = false;
                for (const auto& t : types) if (t == word) isType = true;

                if (isKw)        put(color, fg(fmt::color::magenta) | fmt::emphasis::bold, word);
                else if (isType) put(color, fg(fmt::color::yellow), word);
                else             putPlain(word);
                word = "";
            }
            putPlain(std::string(1, c));
        }
    }
    if (!word.empty()) putPlain(word);
    putPlain("\n");
}

void DiagnosticEngine::reportError(const fin::location& loc, const std::string& msg) {
    reportError(loc, msg, DiagnosticAttribution{});
}

void DiagnosticEngine::reportError(const fin::location& loc, const std::string& msg,
                                   const DiagnosticAttribution& attribution) {
    emitLocatedError(loc, msg, "", attribution, true, "");
}

void DiagnosticEngine::reportErrorFromFile(const fin::location& loc, const std::string& msg,
                                           const std::string& originFile) {
    emitLocatedError(loc, msg, "", DiagnosticAttribution{}, true, originFile);
}

void DiagnosticEngine::reportErrorFromFile(const fin::location& loc, const std::string& msg,
                                           const DiagnosticAttribution& attribution,
                                           const std::string& originFile) {
    emitLocatedError(loc, msg, "", attribution, true, originFile);
}

void DiagnosticEngine::emitLocatedError(const fin::location& loc, const std::string& msg,
                                        const std::string& help,
                                        const DiagnosticAttribution& attribution,
                                        bool runTypoHeuristic,
                                        const std::string& originFile) {
    const RenderSource rs = renderSourceFor(originFile);
    Diagnostic d;
    d.severity = DiagnosticSeverity::Error;
    d.message = msg;
    d.file = rs.file;
    d.line = loc.begin.line;
    d.column = loc.begin.column;
    d.endLine = loc.end.line;
    d.endColumn = loc.end.column;
    d.attribution = attribution;

    if (runTypoHeuristic) {
        std::string badToken = extractTokenText(loc, *rs.lines);
        std::string suggestion = checkTypo(badToken);
        if (suggestion == badToken) suggestion = "";
        if (suggestion.empty()) {
            fin::location prevLoc = getPreviousWordLoc(loc, *rs.lines);
            if (prevLoc.begin.column != loc.begin.column) {
                std::string prevWord = extractTokenText(prevLoc, *rs.lines);
                std::string prevSuggestion = checkTypo(prevWord);
                if (prevSuggestion == prevWord) prevSuggestion = "";
                if (!prevSuggestion.empty()) {
                    d.help = fmt::format("the word '{}' looks suspicious. Did you mean '{}'?",
                                         prevWord, prevSuggestion);
                }
            }
        } else {
            d.help = fmt::format("did you mean '{}'?", suggestion);
        }
    } else {
        // No `checkTypo` pass. The caller supplied the explanation, so the heuristic has
        // nothing to add and one row to lose it in.
        d.help = help;
    }

    errorCount++;
    emit(d);
}

void DiagnosticEngine::reportError(const fin::location& loc, const std::string& msg,
                                   const std::string& help) {
    emitLocatedError(loc, msg, help, DiagnosticAttribution{}, false, "");
}

void DiagnosticEngine::reportErrorFromFile(const fin::location& loc, const std::string& msg,
                                           const std::string& help,
                                           const std::string& originFile) {
    emitLocatedError(loc, msg, help, DiagnosticAttribution{}, false, originFile);
}

void DiagnosticEngine::reportError(const fin::location& loc, const std::string& msg,
                                   const std::string& help,
                                   const DiagnosticAttribution& attribution) {
    emitLocatedError(loc, msg, help, attribution, false, "");
}

void DiagnosticEngine::reportErrorFromFile(const fin::location& loc, const std::string& msg,
                                           const std::string& help,
                                           const DiagnosticAttribution& attribution,
                                           const std::string& originFile) {
    emitLocatedError(loc, msg, help, attribution, false, originFile);
}

void DiagnosticEngine::reportError(const std::string& msg) {
    Diagnostic d;
    d.severity = DiagnosticSeverity::Error;
    d.message = msg;
    // No `d.file`: a diagnostic with no source location is about the invocation,
    // not about a place in a file (ADR 0009), so JSON renders `"file":null`.
    // Setting it to this engine's file produced a third shape -- `file` set with
    // `line` 0, rendered as `mylib.fin:0:0` -- that claims a file without naming
    // a place in it.
    errorCount++;
    emit(d);
}

void DiagnosticEngine::reportError(const std::string& msg, const std::string& help) {
    Diagnostic d;
    d.severity = DiagnosticSeverity::Error;
    d.message = msg;
    // Same as above: locationless means no file, not this engine's file.
    d.help = help;
    errorCount++;
    emit(d);
}

void DiagnosticEngine::reportWarning(const fin::location& loc, const std::string& msg) {
    Diagnostic d;
    d.severity = DiagnosticSeverity::Warning;
    d.message = msg;
    d.file = filename;
    d.line = loc.begin.line;
    d.column = loc.begin.column;
    d.endLine = loc.end.line;
    d.endColumn = loc.end.column;
    warningCount++;
    emit(d);
}

void DiagnosticEngine::reportNote(const fin::location& loc, const std::string& msg) {
    Diagnostic d;
    d.severity = DiagnosticSeverity::Note;
    d.message = msg;
    d.file = filename;
    d.line = loc.begin.line;
    d.column = loc.begin.column;
    d.endLine = loc.end.line;
    d.endColumn = loc.end.column;
    emit(d);
}

void DiagnosticEngine::emit(const Diagnostic& d) {
    Diagnostic coded = d;
    // Central, so every report path — present and future — carries the code
    // without its call site naming one. An explicitly set code always wins; the
    // registry only fills in what the emitter left blank.
    if (coded.code.empty()) coded.code = codeForMessage(coded.message);
    records.push_back(coded);
    if (format == DiagnosticFormat::Json) emitJson(coded);
    else emitHuman(coded);
}

void DiagnosticEngine::emitHuman(const Diagnostic& d) {
    fmt::text_style head = d.severity == DiagnosticSeverity::Error
        ? (fg(fmt::color::red) | fmt::emphasis::bold)
        : (fg(fmt::color::yellow) | fmt::emphasis::bold);

    put(color, head, fmt::format("{}: ", severityName(d.severity)));
    put(color, fmt::text_style(fmt::emphasis::bold), d.message + "\n");

    if (d.line > 0) {
        // The lines come from the file the diagnostic names, which is this
        // engine's own everywhere but on a cross-file render -- and there the
        // record's file is the lookup key that finds the same table the error
        // path chose.
        const RenderSource rs = renderSourceFor(d.file);
        put(color, fg(fmt::color::cornflower_blue),
            fmt::format("   --> {}:{}:{}\n", rs.file, d.line, d.column));
        printContext(d.line, d.column, d.endColumn, *rs.lines);
    } else if (!d.file.empty()) {
        put(color, fg(fmt::color::cornflower_blue), fmt::format("   --> {}\n", d.file));
    }

    if (!d.help.empty()) {
        put(color, fg(fmt::color::cyan), fmt::format("   = help: {}\n", d.help));
    }
}

void DiagnosticEngine::emitJson(const Diagnostic& d) {
    // One object per line, written as it is reported, so a compiler that dies
    // mid-run still leaves a parseable prefix (ADR 0009).
    std::string attribution = "null";
    if (!d.attribution.empty()) {
        attribution = fmt::format("{{\"handler\":{},\"event\":{}}}",
                                  jsonOptional(d.attribution.handler),
                                  jsonOptional(d.attribution.event));
    }
    fmt::print(stderr,
        "{{\"kind\":\"diagnostic\",\"severity\":\"{}\",\"code\":{},\"message\":{},"
        "\"file\":{},\"line\":{},\"column\":{},\"endLine\":{},\"endColumn\":{},"
        "\"help\":{},\"attribution\":{}}}\n",
        severityName(d.severity),
        jsonOptional(d.code),
        jsonString(d.message),
        jsonOptional(d.file),
        d.line, d.column, d.endLine, d.endColumn,
        jsonOptional(d.help),
        attribution);
    std::fflush(stderr);
}

void DiagnosticEngine::emitSummary(int exitCode) {
    if (format != DiagnosticFormat::Json) return;
    fmt::print(stderr,
        "{{\"kind\":\"summary\",\"errors\":{},\"warnings\":{},\"exitCode\":{},\"status\":\"{}\"}}\n",
        errorCount, warningCount, exitCode, exitCode == 0 ? "ok" : "failed");
    std::fflush(stderr);
}

void DiagnosticEngine::note(const std::string& text) {
    if (format == DiagnosticFormat::Json) return;
    putPlain(text + "\n");
}

void DiagnosticEngine::success(const std::string& text) {
    if (format == DiagnosticFormat::Json) return;
    put(color, fg(fmt::color::green) | fmt::emphasis::bold, text + "\n");
}

void DiagnosticEngine::printContext(int lineNum, int beginCol, int endCol,
                                     const std::vector<std::string>& srcLines) {
    std::string lineContent = lineIn(srcLines, lineNum);
    std::string lineNumStr = std::to_string(lineNum);
    std::string padding(lineNumStr.length(), ' ');

    put(color, fg(fmt::color::cornflower_blue), fmt::format(" {} |\n", padding));
    put(color, fg(fmt::color::cornflower_blue), fmt::format(" {} | ", lineNumStr));
    printHighlightedLine(lineContent);
    put(color, fg(fmt::color::cornflower_blue), fmt::format(" {} | ", padding));

    for (int i = 1; i < beginCol; i++) putPlain(" ");

    int len = std::max(1, endCol - beginCol);
    put(color, fg(fmt::color::red) | fmt::emphasis::bold, std::string(len, '^'));
    put(color, fg(fmt::color::red) | fmt::emphasis::bold, " here\n");
}

}
