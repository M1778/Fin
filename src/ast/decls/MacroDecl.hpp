#pragma once
#include "../nodes/ASTNode.hpp"
#include "../stmts/Statement.hpp" // For Block
#include "../types/Attribute.hpp"
#include <vector>
#include <string>
#include <memory>

namespace fin {

class Scope;

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

    // The scope of the module that declared this macro, or null for a macro declared in
    // the file being compiled (ADR 0023 step 4).
    //
    // A body's non-`$` names resolve here and not at the call site: `coll!` spelling
    // `Collection::from_prototype` has to work in a caller that never imported
    // `Collection`, because it is `lib/std/collection.fin` that imported what the body
    // needs. Under call-site resolution such a macro works only by luck of its caller's
    // imports, which is ADR 0020's C-preprocessor failure arriving unmodified.
    //
    // `Scope*` and not `shared_ptr<Scope>`, because what this points at is
    // `ModuleLoader::moduleCache`'s entry -- the analyzer scope, kept for the life of
    // the loader -- and the loader outlives every expansion. The expander's own
    // `macroScope` is the pointer that would dangle: it dies at the return of
    // `loadModule`, so `visit(ImportModule&)` stores the scope `loadModule` *returned*
    // and never the one it made. A shared_ptr here would put a strong edge from an AST
    // node into a scope graph that already owns AST nodes back (`Scope::macros` holds
    // `MacroDeclaration*`), which is a cycle rather than safety.
    Scope* declaringScope = nullptr;

    MacroDeclaration(std::string n, std::vector<MacroParam> p, std::unique_ptr<Block> b);
    void accept(Visitor& v) override;
};

}
