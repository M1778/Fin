#include "GenericParam.hpp"
#include "../Visitor.hpp"

namespace fin {

GenericParam::GenericParam(std::string n, std::unique_ptr<TypeNode> c) 
    : name(std::move(n)), constraint(std::move(c)) {}
void GenericParam::accept(Visitor& v) { v.visit(*this); }

} // namespace fin
