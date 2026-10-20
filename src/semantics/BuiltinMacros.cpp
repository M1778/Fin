// SPDX-License-Identifier: GPL-3.0-or-later
#include "BuiltinMacros.hpp"

namespace fin::builtinmacros {

namespace {

std::vector<Builtin> build() {
    std::vector<Builtin> bs;

    // --- format!(fmt: string, ...) <string> ---
    //
    // The format string is a parameter and not a literal, which is why this is a
    // builtin and not a library macro. `tests/samples/stdlib/stdio.fin:35-36` writes
    // `pub fun printf<X: Any<Printable>>(fmt: string, ...objects: [X])` and then
    // `format!(fmt, ...objects)` inside it -- `fmt` there is a runtime value, so a
    // macro that pasted a literal into a template could not serve that call. ADR 0021
    // records this as the reason the visibility question could not be answered by
    // making the macro namespace ambient.
    //
    // `<string>` and not `<noret>`: `format!` builds a value. `printf` is the one that
    // returns nothing, and it is a symbol rather than a macro (`lib/std/stdio.fin:109`).
    bs.push_back(Builtin{"format", {Param{"fmt", "string"}}, true, "string"});

    // --- enum_from!(value) <string> ---
    //
    // The member-name op the reflection refusals promise ("the member ops come
    // with enum_from", ADR 0057): answers an enum value's member name, the leg
    // `getkeyid`/`keyidof` do not (neither names a member). It is a builtin and
    // not a library macro for the reason `format!` is: the expansion depends on
    // facts the expander does not have -- the enum's member list -- so a
    // registered macro cannot construct it, and the analyzer/codegen passes
    // implement the call instead. lib/std/enums.fin declares the signature, the
    // way stdio.fin declares `format!`'s. ADR 0059.
    bs.push_back(Builtin{"enum_from", {Param{"value", "EnumType"}}, false, "string"});

    return bs;
}

} // namespace

const std::vector<Builtin>& all() {
    static const std::vector<Builtin> table = build();
    return table;
}

const Builtin* find(const std::string& name) {
    for (const auto& b : all())
        if (b.name == name) return &b;
    return nullptr;
}

size_t minArgs(const Builtin& b) {
    return b.params.size();
}

std::string signatureOf(const Builtin& b) {
    std::string s = b.name + "!(";
    for (size_t i = 0; i < b.params.size(); ++i) {
        if (i) s += ", ";
        s += b.params[i].name + ": " + b.params[i].type;
    }
    if (b.is_variadic) {
        if (!b.params.empty()) s += ", ";
        s += "...";
    }
    s += ") <" + b.return_type + ">";
    return s;
}

} // namespace fin::builtinmacros
