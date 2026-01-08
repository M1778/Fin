#pragma once
#include "../nodes/ASTNode.hpp"
#include "FunctionDecl.hpp"
#include "../types/TypeNode.hpp"
#include "../types/GenericParam.hpp"
#include "../types/Attribute.hpp"
#include <vector>
#include <string>
#include <memory>
#include <utility>

namespace fin {

class StructMember : public ASTNode {
public:
    std::string name;
    std::unique_ptr<TypeNode> type;
    std::unique_ptr<Expression> default_value;
    bool is_public;
    std::vector<std::unique_ptr<Attribute>> attributes;
    StructMember(std::string n, std::unique_ptr<TypeNode> t, bool pub);
    void accept(Visitor&) override;
};

class StructDeclaration : public Statement {
public:
    std::string name;
    std::vector<std::unique_ptr<StructMember>> members;
    std::vector<std::unique_ptr<FunctionDeclaration>> methods;
    std::vector<std::unique_ptr<OperatorDeclaration>> operators;
    std::vector<std::unique_ptr<ConstructorDeclaration>> constructors;
    std::unique_ptr<DestructorDeclaration> destructor;
    std::vector<std::unique_ptr<GenericParam>> generic_params;
    std::vector<std::unique_ptr<Attribute>> attributes;
    std::vector<std::unique_ptr<TypeNode>> parents;
    bool is_public;
    bool is_class = false; // True if declared with 'class' keyword
    
    StructDeclaration(std::string n, std::vector<std::unique_ptr<StructMember>> m, bool pub);
    void accept(Visitor& v) override;
};

class InterfaceDeclaration : public Statement {
public:
    std::string name;
    std::vector<std::unique_ptr<StructMember>> members;
    std::vector<std::unique_ptr<FunctionDeclaration>> methods;
    std::vector<std::unique_ptr<OperatorDeclaration>> operators;
    std::vector<std::unique_ptr<ConstructorDeclaration>> constructors;
    std::unique_ptr<DestructorDeclaration> destructor;
    
    std::vector<std::unique_ptr<Attribute>> attributes;
    std::vector<std::unique_ptr<GenericParam>> generic_params;
    bool is_public;
    
    InterfaceDeclaration(std::string n, 
                         std::vector<std::unique_ptr<StructMember>> m, 
                         std::vector<std::unique_ptr<FunctionDeclaration>> f, 
                         std::vector<std::unique_ptr<OperatorDeclaration>> o,
                         std::vector<std::unique_ptr<ConstructorDeclaration>> c,
                         std::unique_ptr<DestructorDeclaration> d,
                         bool pub);
    void accept(Visitor& v) override;
};

class EnumDeclaration : public Statement {
public:
    std::string name;
    std::vector<std::pair<std::string, std::unique_ptr<Expression>>> values;
    std::vector<std::unique_ptr<Attribute>> attributes;
    bool is_public;
    EnumDeclaration(std::string n, std::vector<std::pair<std::string, std::unique_ptr<Expression>>> v, bool pub);
    void accept(Visitor& v) override;
};

}
