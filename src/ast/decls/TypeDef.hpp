#pragma once
#include "../nodes/ASTNode.hpp"
#include "../nodes/Parameter.hpp"
#include "../stmts/Statement.hpp"
#include "../types/TypeNode.hpp"
#include "../types/GenericParam.hpp"
#include "../types/Attribute.hpp"
#include <vector>
#include <string>
#include <memory>

namespace fin {

class Parameter;
class Block;

// Represents: type Name<T> = <SomeType>;
// Also handles: type Any<...> = any implements <...>;
class TypeDefinition : public Statement {
public:
    std::string name;
    std::vector<std::unique_ptr<GenericParam>> generic_params;
    std::unique_ptr<TypeNode> aliased_type;
    std::vector<std::unique_ptr<Attribute>> attributes;
    bool is_public = false;
    
    // For "implements" constraints: type Any<...> = any implements <X, Y>;
    bool has_implements = false;
    std::vector<std::unique_ptr<TypeNode>> implements_list;
    
    // `pub implements c_printf = printf;` -- tests/samples/stdlib/stdio.fin:15.
    // The name is bound to another symbol and not to a type: `aliased_type` holds
    // the right-hand name, and this says to read it as a symbol.
    bool is_symbol_resolution = false;
    
    // `extern myns::myfunc as myfunc;` -- tests/samples/extern_as.fin:19. Like
    // `is_symbol_resolution`, the name is bound to another symbol rather than to a
    // type, but the direction is reversed: here `aliased_type` holds the existing
    // path and `name` the new name.
    bool is_extern_alias = false;
    // `extern * from a_namespace;` (extern_as.fin:32, :39): every name in the
    // namespace or enum named by `aliased_type` becomes visible here, and `name`
    // is `*` because there is no single new name.
    bool is_extern_wildcard = false;
    
    // Union alias: `type Number = int | uint | float | ...;`
    // (tests/samples/arrays.fin:9, stdlib/types.fin:53, stdlib/typing.fin:10).
    // The alternatives after the first; `aliased_type` is the first, so a pass
    // that does not know about unions still reads one type instead of none.
    std::vector<std::unique_ptr<TypeNode>> union_members;
    
    TypeDefinition(std::string n, std::unique_ptr<TypeNode> t);
    void accept(Visitor& v) override;
};

// Represents: @special function_name(...) <RetType> { ... }
class SpecialDeclaration : public Statement {
public:
    std::string name;
    std::vector<std::unique_ptr<Parameter>> params;
    std::unique_ptr<TypeNode> return_type;
    std::unique_ptr<Block> body;
    std::vector<std::unique_ptr<Attribute>> attributes;
    // `@special(pub)` / `@special(priv)` -- the standard library declares the
    // visibility of a special function inside the header's parentheses
    // (stdlib/types.fin:24, stdlib/error.fin:26, stdlib/enums.fin:12).
    bool is_public = false;
    
    SpecialDeclaration(std::string n, std::vector<std::unique_ptr<Parameter>> p,
                       std::unique_ptr<TypeNode> rt, std::unique_ptr<Block> b);
    void accept(Visitor& v) override;
};

}
