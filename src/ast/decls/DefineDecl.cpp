#include "DefineDecl.hpp"
#include "../Visitor.hpp"

namespace fin {

DefineDeclaration::DefineDeclaration(std::string n, std::vector<std::unique_ptr<Parameter>> p, std::unique_ptr<TypeNode> rt, bool v)
    : name(std::move(n)), params(std::move(p)), return_type(std::move(rt)), is_vararg(v) {}
void DefineDeclaration::accept(Visitor& v) { v.visit(*this); }

}
