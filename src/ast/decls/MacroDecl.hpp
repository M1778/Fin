#pragma once
#include "../nodes/ASTNode.hpp"
#include "../stmts/Statement.hpp" // For Block
#include "../types/Attribute.hpp"
#include <vector>
#include <string>
#include <memory>

namespace fin {

struct MacroParam {
    std::string name;
    std::string type; // "expr", "block", "ident"
    bool is_vararg = false; 
};

// One form: named parameters and a body that returns one `quote`d expression
// (ADR 0023). `MacroRule` and the `rules`/`is_rust_style` fields were the arms form's
// half of this node and are deleted with its grammar -- a `pattern` that was one
// `std::string` could not hold `$x:expr`, and the constructor that filled it left
// `body` null, which is what made an invocation a segfault rather than a diagnostic.
//
// `body` is null only for a bodyless declaration -- `@define format!(...) <string>;`,
// a macro the compiler implements -- and MacroExpander refuses to expand one, because
// there is no template to substitute into.
class MacroDeclaration : public Statement {
public:
    std::string name;
    std::vector<MacroParam> params;
    std::unique_ptr<Block> body;
    std::vector<std::unique_ptr<Attribute>> attributes;

    MacroDeclaration(std::string n, std::vector<MacroParam> p, std::unique_ptr<Block> b);
    void accept(Visitor& v) override;
};

}
