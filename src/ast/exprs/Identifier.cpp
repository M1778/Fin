#include "Identifier.hpp"
#include "../Visitor.hpp"

namespace fin {
Identifier::Identifier(std::string n) : name(std::move(n)) {}
void Identifier::accept(Visitor& v) { v.visit(*this); }
}
