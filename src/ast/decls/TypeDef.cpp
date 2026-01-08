#include "TypeDef.hpp"

namespace fin {

TypeDefinition::TypeDefinition(std::string n, std::unique_ptr<TypeNode> t)
    : name(std::move(n)), aliased_type(std::move(t)) {}

void TypeDefinition::accept(Visitor& v) { v.visit(*this); }

SpecialDeclaration::SpecialDeclaration(std::string n, std::vector<std::unique_ptr<Parameter>> p,
                                       std::unique_ptr<TypeNode> rt, std::unique_ptr<Block> b)
    : name(std::move(n)), params(std::move(p)), return_type(std::move(rt)), body(std::move(b)) {}

void SpecialDeclaration::accept(Visitor& v) { v.visit(*this); }

}
