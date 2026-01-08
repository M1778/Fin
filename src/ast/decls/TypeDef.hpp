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
    
    SpecialDeclaration(std::string n, std::vector<std::unique_ptr<Parameter>> p,
                       std::unique_ptr<TypeNode> rt, std::unique_ptr<Block> b);
    void accept(Visitor& v) override;
};

}
