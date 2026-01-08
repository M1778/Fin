#include "UnaryOp.hpp"
#include "../Visitor.hpp"

namespace fin {
UnaryOp::UnaryOp(ASTTokenKind o, std::unique_ptr<Expression> e) : op(o), operand(std::move(e)) {}
void UnaryOp::accept(Visitor& v) { v.visit(*this); }
}
