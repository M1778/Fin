#include "Literal.hpp"
#include "../Visitor.hpp"

namespace fin {
Literal::Literal(std::string v, ASTTokenKind k) : value(std::move(v)), kind(k) {}
void Literal::accept(Visitor& v) { v.visit(*this); }
}
