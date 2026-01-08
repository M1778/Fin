#include "ClassDecl.hpp"
#include "../Visitor.hpp"

namespace fin {

ClassDeclaration::ClassDeclaration(std::string n, std::vector<std::unique_ptr<StructMember>> m, bool pub)
    : name(std::move(n)), members(std::move(m)), is_public(pub) {}

void ClassDeclaration::accept(Visitor& v) { v.visit(*this); }

}
