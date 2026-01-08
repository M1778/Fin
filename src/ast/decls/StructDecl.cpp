#include "StructDecl.hpp"
#include "../Visitor.hpp"

namespace fin {

StructMember::StructMember(std::string n, std::unique_ptr<TypeNode> t, bool pub)
    : name(std::move(n)), type(std::move(t)), is_public(pub) {}
void StructMember::accept(Visitor& v) { v.visit(*this); }

StructDeclaration::StructDeclaration(std::string n, std::vector<std::unique_ptr<StructMember>> m, bool pub)
    : name(std::move(n)), members(std::move(m)), is_public(pub) {}
void StructDeclaration::accept(Visitor& v) { v.visit(*this); }

InterfaceDeclaration::InterfaceDeclaration(std::string n, 
                     std::vector<std::unique_ptr<StructMember>> m, 
                     std::vector<std::unique_ptr<FunctionDeclaration>> f, 
                     std::vector<std::unique_ptr<OperatorDeclaration>> o,
                     std::vector<std::unique_ptr<ConstructorDeclaration>> c,
                     std::unique_ptr<DestructorDeclaration> d,
                     bool pub)
    : name(std::move(n)), members(std::move(m)), methods(std::move(f)), 
      operators(std::move(o)), constructors(std::move(c)), destructor(std::move(d)), 
      is_public(pub) {}
void InterfaceDeclaration::accept(Visitor& v) { v.visit(*this); }

EnumDeclaration::EnumDeclaration(std::string n, std::vector<std::pair<std::string, std::unique_ptr<Expression>>> v, bool pub)
    : name(std::move(n)), values(std::move(v)), is_public(pub) {}
void EnumDeclaration::accept(Visitor& v) { v.visit(*this); }

}
