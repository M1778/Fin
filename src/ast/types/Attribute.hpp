#pragma once

#include "../nodes/ASTNode.hpp"
#include <memory>
#include <string>
#include <vector>

namespace fin {

class Statement;

// The attribute that makes a declaration visible with no import (ADR 0021's
// ruling). Spelled once, here, because the parser marks it, the analyzer refuses
// a misplaced one and the module loader harvests it -- three passes that have to
// agree on a string.
inline constexpr const char* kGlobalAttribute = "global";

class Attribute : public ASTNode {
public:
    std::string name;
    std::string value_str;
    bool is_flag = true;

    // Whether this attribute was written inside a `namespace std { ... }` block.
    //
    // It lives on the attribute rather than on the declaration because the parser
    // is the only pass that can answer it: `namespace_block` splices its contents
    // into the enclosing statement list and discards the name (parser.y), so by
    // the time the analyzer sees a declaration there is nothing left to say which
    // namespace it was written in. The parser sets this while the name is still in
    // hand; whoever enforces a namespace-restricted attribute reads it.
    bool std_scoped = false;

    Attribute(std::string n, bool flag);
    Attribute(std::string n, std::string v);
    void accept(Visitor& v) override;
};

// Marks every `#[global]` reachable from `body` as written inside a `namespace
// std`. Called from the `namespace_block` production for a block named `std`.
//
// Deep rather than one level, because the grammar accepts an attribute on a
// declaration anywhere a statement is accepted -- including inside a function
// body -- so a rule enforced only over a namespace's immediate statements would
// let a nested one through. The traversal is StructuralWalk's, which is the one
// place that knows every node's children (ADR 0004).
void markStdScopedGlobals(const std::vector<std::unique_ptr<Statement>>& body);

} // namespace fin
