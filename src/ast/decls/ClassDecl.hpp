#pragma once
#include "../nodes/ASTNode.hpp"
#include "FunctionDecl.hpp"
#include "../types/TypeNode.hpp"
#include "../types/GenericParam.hpp"
#include "../types/Attribute.hpp"
#include "StructDecl.hpp" // For StructMember
#include <vector>
#include <string>
#include <memory>
#include <utility>

namespace fin {

class ClassDeclaration : public Statement {
public:
    std::string name;
    std::vector<std::unique_ptr<StructMember>> members; // Reusing StructMember for now
    std::vector<std::unique_ptr<FunctionDeclaration>> methods;
    std::vector<std::unique_ptr<OperatorDeclaration>> operators;
    std::vector<std::unique_ptr<ConstructorDeclaration>> constructors;
    std::unique_ptr<DestructorDeclaration> destructor;
    std::vector<std::unique_ptr<GenericParam>> generic_params;
    std::vector<std::unique_ptr<Attribute>> attributes;
    std::vector<std::unique_ptr<TypeNode>> parents;
    bool is_public;
    
    ClassDeclaration(std::string n, std::vector<std::unique_ptr<StructMember>> m, bool pub);
    void accept(Visitor& v) override;
};

}
