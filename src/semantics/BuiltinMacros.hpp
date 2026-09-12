#pragma once
#include <string>
#include <vector>

namespace fin {

// The macros the compiler implements, as data (ADR 0023 step 6).
//
// A macro reaches the analyzer in one of two states. `@macro name(a) { return quote
// { ... }; }` has a template, and the expander answers the call by substituting into
// it -- the analyzer never sees the invocation, only the expansion. A bodyless
// `@define name!(...) <T>;` has no template, so there is nothing to substitute and
// the expander leaves the invocation alone; the call is answered here instead. This
// table is what "answered here" means: it is the list of names for which that is
// true, and the signature each one promises.
//
// A table rather than cases in the analyzer for the same reason `compilerapi`'s
// inventory is one: the second builtin should be a row and not a branch. It has one
// row today. `format` is the only compiler-implemented macro the corpus writes --
// `map!` and `coll!` are library macros with bodies (ADR 0023 step 8), and nothing
// in the tree writes a third.
//
// Two consequences of a name being in here, and they are why the table is consulted
// from two passes rather than one:
//
//   * It resolves with no import, in any file, because it is not in a scope at all.
//     `deeptest2.fin` and `stdlib/error.fin` call `format!` with zero import lines
//     between them, so bare resolution is not a convenience -- ADR 0021 measured it
//     as a requirement and ruled `format!` a builtin on that evidence. The way
//     `cast`, `sizeof` and `new` are ambient in the expression grammar.
//   * A bodyless declaration of a name *not* in here is refused where it is written.
//     `@define frobnicate!(a: int) <int>;` claims the compiler implements a macro the
//     compiler has never heard of; the alternative is a declaration that parses,
//     type-checks and then reports nothing at every call, which is a promise the
//     compiler cannot keep and does not say so.
namespace builtinmacros {

// One fixed parameter. `type` is a spelling resolved through the analyzer's scope,
// so it is the same type a program can write -- there is no parallel type table
// here, and `string` below means exactly what `let s <string>` means.
struct Param {
    std::string name;
    std::string type;
};

struct Builtin {
    // As written, without the `!`. `MacroInvocation::name` holds the same spelling.
    std::string name;
    // The parameters whose type is fixed, in order. Each one is required: there is
    // no optional or defaulted parameter in this table, because a macro parameter
    // has no default to write (`MacroParam` holds a name and a fragment kind).
    std::vector<Param> params;
    // A trailing `...`, taking any number of arguments of any type. Unchecked by
    // design and not by omission: `format!("{} {}", n, name)` passes an `int` and a
    // `string` to one call, and what each one has to be is the codegen's business
    // (ADR 0023 step 7) rather than a type this table could name.
    bool is_variadic = false;
    // The type of the expression the invocation becomes.
    std::string return_type;
};

const std::vector<Builtin>& all();

// Null when the name is not a compiler-implemented macro, which is the answer for
// every macro a program declares itself.
const Builtin* find(const std::string& name);

// The minimum number of arguments a call must pass: every fixed parameter. Equal to
// `params.size()`, and spelled as a function because a reader of an arity diagnostic
// should be able to find the one place the number comes from.
size_t minArgs(const Builtin& b);

// `format!(fmt: string, ...) <string>` -- the signature as a program would write it,
// for a diagnostic that has to show what the compiler expected.
std::string signatureOf(const Builtin& b);

} // namespace builtinmacros
} // namespace fin
