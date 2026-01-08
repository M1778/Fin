#include "MiscExpr.hpp"
#include "../Visitor.hpp"

namespace fin {

CastExpression::CastExpression(std::unique_ptr<TypeNode> t, std::unique_ptr<Expression> e)
    : target_type(std::move(t)), expr(std::move(e)) {}
void CastExpression::accept(Visitor& v) { v.visit(*this); }

SizeofExpression::SizeofExpression(std::unique_ptr<TypeNode> t) : type_target(std::move(t)) {}
SizeofExpression::SizeofExpression(std::unique_ptr<Expression> e) : expr_target(std::move(e)) {}
void SizeofExpression::accept(Visitor& v) { v.visit(*this); }

TernaryOp::TernaryOp(std::unique_ptr<Expression> c, std::unique_ptr<Expression> t, std::unique_ptr<Expression> f)
    : condition(std::move(c)), true_expr(std::move(t)), false_expr(std::move(f)) {}
void TernaryOp::accept(Visitor& v) { v.visit(*this); }

// Case 1: super { ... }
SuperExpression::SuperExpression(std::vector<std::pair<std::string, std::unique_ptr<Expression>>> f)
    : init_fields(std::move(f)) {}

// Case 2: super::Parent(...) or super(...)
SuperExpression::SuperExpression(std::string p, std::vector<std::unique_ptr<Expression>> a)
    : parent_name(std::move(p)), args(std::move(a)) {}

// Case 3: super::Parent { ... }
SuperExpression::SuperExpression(std::string p, std::vector<std::pair<std::string, std::unique_ptr<Expression>>> f)
    : init_fields(std::move(f)), parent_name(std::move(p)) {}

void SuperExpression::accept(Visitor& v) { v.visit(*this); }

MacroInvocation::MacroInvocation(std::string n, std::vector<std::unique_ptr<Expression>> a)
    : name(std::move(n)), args(std::move(a)) {}
void MacroInvocation::accept(Visitor& v) { v.visit(*this); }

}
