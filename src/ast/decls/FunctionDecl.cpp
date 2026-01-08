#include "FunctionDecl.hpp"
#include "../Visitor.hpp"

namespace fin {

FunctionDeclaration::FunctionDeclaration(std::string n, std::vector<std::unique_ptr<Parameter>> p, std::unique_ptr<TypeNode> rt, std::unique_ptr<Block> b)
    : name(std::move(n)), params(std::move(p)), return_type(std::move(rt)), body(std::move(b)) {}
void FunctionDeclaration::accept(Visitor& v) { v.visit(*this); }

OperatorDeclaration::OperatorDeclaration(ASTTokenKind o, std::vector<std::unique_ptr<Parameter>> p, 
                    std::unique_ptr<TypeNode> rt, std::unique_ptr<Block> b, bool pub)
    : op(o), params(std::move(p)), return_type(std::move(rt)), body(std::move(b)), is_public(pub), implements_type(nullptr) {}
void OperatorDeclaration::accept(Visitor& v) { v.visit(*this); }

ConstructorDeclaration::ConstructorDeclaration(std::string n, std::vector<std::unique_ptr<Parameter>> p, std::unique_ptr<Block> b, std::unique_ptr<TypeNode> rt)
    : name(std::move(n)), params(std::move(p)), body(std::move(b)), return_type(std::move(rt)) {}
void ConstructorDeclaration::accept(Visitor& v) { v.visit(*this); }

DestructorDeclaration::DestructorDeclaration(std::string n, std::unique_ptr<Block> b)
    : name(std::move(n)), body(std::move(b)) {}
void DestructorDeclaration::accept(Visitor& v) { v.visit(*this); }

}
